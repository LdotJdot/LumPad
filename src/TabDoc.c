// encoding: UTF-8
#include "Helpers.h"
#include <commctrl.h>
#include <string.h>
#include "PathLib.h"
#include "SciCall.h"
#include "Encoding.h"
#include "Edit.h"
#include "Notepad3.h"
#include "Dialogs.h"
#include "DarkMode/DarkMode.h"
#include "MuiLanguage.h"
#include "Styles.h"
#include "TabDoc.h"
#include "resource.h"
#include "Config/Config.h"
#include <windowsx.h>

#define LUMPAD_MAX_TABS 48

typedef struct LUMPAD_TAB {
    sptr_t     doc;
    HPATHL     path;
    FILEVARS   fv;
    cpi_enc_t  encoding;
    int        eolMode;
    bool       dirty; // last-known modify flag (non-active tabs; active uses SciCall_GetModify in UI)
    WIN32_FILE_ATTRIBUTE_DATA fileSnap; // disk size + mtime when tab state was last saved to slot
    bool       fileSnapValid;
} LUMPAD_TAB;

static LUMPAD_TAB s_tabs[LUMPAD_MAX_TABS];
static int        s_tabCount = 0;
static int        s_activeTab = 0;
static HWND       s_hwndTab = NULL;
static HWND       s_hwndBtnNew = NULL;
static HWND       s_hwndTabRowFill = NULL;
static int        s_hotCloseTab = -1;
static bool       s_tabMouseLeaveTracked = false;
static WCHAR      s_tabTooltipBuf[MAX_PATH_EXPLICIT];
static int        s_stripLayoutX = 0;
static int        s_stripLayoutY = 0;
static int        s_stripLayoutCx = 0;
static HFONT      s_hfTabGlyph = NULL;
static int        s_closeCaptureTab = -1;
static RECT       s_tabRects[LUMPAD_MAX_TABS];
static HWND       s_hwndTabTips = NULL;
static int        s_tooltipHoverIdx = -1;
static HFONT      s_hfPlusBtn = NULL;

#define SUBCID_TABSTRIP 1u
#define SUBCID_TABROWFILL 2u
#define SUBCID_PLUSBTN 3u

static bool _TabDoc_FileSnapEqual(WIN32_FILE_ATTRIBUTE_DATA const* a, WIN32_FILE_ATTRIBUTE_DATA const* b)
{
    return (a->nFileSizeLow == b->nFileSizeLow) && (a->nFileSizeHigh == b->nFileSizeHigh)
        && (a->ftLastWriteTime.dwLowDateTime == b->ftLastWriteTime.dwLowDateTime)
        && (a->ftLastWriteTime.dwHighDateTime == b->ftLastWriteTime.dwHighDateTime);
}

static void _TabDoc_ReadPathFileSnap(HPATHL hp, WIN32_FILE_ATTRIBUTE_DATA* out, bool* valid)
{
    ZeroMemory(out, sizeof(*out));
    *valid = false;
    if (!hp || Path_IsEmpty(hp) || !Path_IsExistingFile(hp)) {
        return;
    }
    if (Path_GetFileAttributesEx(hp, GetFileExInfoStandard, out)) {
        *valid = true;
    }
}

static int _TabDoc_CloseColumnPx(void)
{
    if (!s_hwndTab || !IsWindow(s_hwndTab)) {
        return 16;
    }
    return ScaleIntToDPI(s_hwndTab, 16);
}

static void _TabDoc_FormatTabTitle(int idx, WCHAR* wchBuf, size_t cch)
{
    if (idx < 0 || idx >= s_tabCount || !wchBuf || (cch == 0)) {
        if (wchBuf && (cch > 0)) {
            wchBuf[0] = L'\0';
        }
        return;
    }
    if (Path_IsNotEmpty(s_tabs[idx].path)) {
        // Tab strip: file name only; full path on hover (tooltip).
        Path_GetDisplayName(wchBuf, (DWORD)cch, s_tabs[idx].path, NULL, true);
    } else {
        GetLngString(IDS_MUI_UNTITLED, wchBuf, (int)cch);
    }
    bool const modified = (idx == s_activeTab) ? SciCall_GetModify() : s_tabs[idx].dirty;
    if (modified) {
        WCHAR wchStar[MAX_PATH_EXPLICIT + 4] = { L'\0' };
        StringCchPrintf(wchStar, COUNTOF(wchStar), L"*%s", wchBuf);
        StringCchCopyN(wchBuf, cch, wchStar, cch);
    }
}

static void _TabDoc_FormatTabTooltip(int idx, WCHAR* wchBuf, size_t cch)
{
    if (idx < 0 || idx >= s_tabCount || !wchBuf || (cch == 0)) {
        if (wchBuf && (cch > 0)) {
            wchBuf[0] = L'\0';
        }
        return;
    }
    if (Path_IsNotEmpty(s_tabs[idx].path)) {
        WCHAR wchName[MAX_PATH_EXPLICIT] = { L'\0' };
        Path_GetDisplayName(wchName, COUNTOF(wchName), s_tabs[idx].path, NULL, true);
        LPCWSTR const wchFull = Path_Get(s_tabs[idx].path);
        (void)StringCchPrintfW(wchBuf, cch, L"%s\n%s", wchName, wchFull);
    } else {
        GetLngString(IDS_MUI_UNTITLED, wchBuf, (int)cch);
    }
}

static HFONT _TabDoc_EnsureGlyphFont(void)
{
    if (s_hfTabGlyph && IsWindow(s_hwndTab)) {
        return s_hfTabGlyph;
    }
    if (s_hfTabGlyph) {
        DeleteObject(s_hfTabGlyph);
        s_hfTabGlyph = NULL;
    }
    if (!s_hwndTab || !IsWindow(s_hwndTab)) {
        return NULL;
    }
    LOGFONTW lf = { 0 };
    lf.lfHeight = -ScaleIntToDPI(s_hwndTab, 10);
    lf.lfWeight = FW_SEMIBOLD;
    lf.lfCharSet = DEFAULT_CHARSET;
    StringCchCopyW(lf.lfFaceName, LF_FACESIZE, L"Segoe UI");
    s_hfTabGlyph = CreateFontIndirectW(&lf);
    return s_hfTabGlyph;
}

static void _PlusBtnDeleteFont(void)
{
    if (s_hfPlusBtn) {
        DeleteObject(s_hfPlusBtn);
        s_hfPlusBtn = NULL;
    }
}

static void _PlusBtnEnsureFont(HWND hwnd)
{
    if (!hwnd || !IsWindow(hwnd) || s_hfPlusBtn) {
        return;
    }
    LOGFONTW lf = { 0 };
    lf.lfHeight = -ScaleIntToDPI(hwnd, 14);
    lf.lfWeight = FW_SEMIBOLD;
    lf.lfCharSet = DEFAULT_CHARSET;
    StringCchCopyW(lf.lfFaceName, LF_FACESIZE, L"Segoe UI");
    s_hfPlusBtn = CreateFontIndirectW(&lf);
}

static LRESULT CALLBACK _PlusBtnSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    UNREFERENCED_PARAMETER(uIdSubclass);
    HWND const hwndMain = (HWND)dwRefData;
    switch (uMsg) {
    case WM_ERASEBKGND: {
        RECT rc = { 0 };
        GetClientRect(hwnd, &rc);
        bool const dm = UseDarkMode();
        HBRUSH const br = CreateSolidBrush(GetModeBtnfaceColor(dm));
        if (br) {
            FillRect((HDC)wParam, &rc, br);
            DeleteObject(br);
        }
        return 1;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps = { 0 };
        HDC const hdc = BeginPaint(hwnd, &ps);
        if (hdc) {
            RECT rc = { 0 };
            GetClientRect(hwnd, &rc);
            bool const dm = UseDarkMode();
            HBRUSH const br = CreateSolidBrush(GetModeBtnfaceColor(dm));
            if (br) {
                FillRect(hdc, &rc, br);
                DeleteObject(br);
            }
            HPEN const pen = CreatePen(PS_SOLID, 1, dm ? RGB(66, 66, 72) : RGB(190, 190, 198));
            if (pen) {
                HPEN const op = (HPEN)SelectObject(hdc, pen);
                HBRUSH const ob = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
                int const rr = ScaleIntToDPI(hwnd, 3);
                (void)RoundRect(hdc, rc.left, rc.top, rc.right - 1, rc.bottom - 1, rr, rr);
                SelectObject(hdc, ob);
                SelectObject(hdc, op);
                DeleteObject(pen);
            }
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, GetModeTextColor(dm));
            if (!s_hfPlusBtn) {
                _PlusBtnEnsureFont(hwnd);
            }
            HFONT const ho = s_hfPlusBtn ? (HFONT)SelectObject(hdc, s_hfPlusBtn) : NULL;
            DrawTextW(hdc, L"+", 1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            if (ho) {
                SelectObject(hdc, ho);
            }
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_LBUTTONUP:
        SendMessageW(hwndMain, WM_COMMAND, MAKEWPARAM(IDC_TABBTN_NEW, BN_CLICKED), (LPARAM)hwnd);
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) {
            SetCursor(LoadCursorW(NULL, IDC_HAND));
            return TRUE;
        }
        break;
    default:
        break;
    }
    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

static void _TabDoc_DeleteGlyphFont(void)
{
    _PlusBtnDeleteFont();
    if (s_hfTabGlyph) {
        DeleteObject(s_hfTabGlyph);
        s_hfTabGlyph = NULL;
    }
}

static void _StripLayoutItemRects(void)
{
    if (!s_hwndTab || !IsWindow(s_hwndTab) || (s_tabCount <= 0)) {
        return;
    }
    RECT rcCli = { 0 };
    GetClientRect(s_hwndTab, &rcCli);
    int const clientW = max_i(0, rcCli.right - rcCli.left);
    int const clientH = max_i(0, rcCli.bottom - rcCli.top);
    int const padInnerH = ScaleIntToDPI(s_hwndTab, 3);
    int const y0 = padInnerH / 2;
    int const itemH = max_i(ScaleIntToDPI(s_hwndTab, 18), clientH - padInnerH);
    int const arrowFudge = ScaleIntToDPI(s_hwndTab, 6);
    int const usable = max_i(0, clientW - arrowFudge);
    int const fairShare = (s_tabCount > 0) ? (usable / s_tabCount) : usable;
    int const minW = ScaleIntToDPI(s_hwndTab, 56);
    int const maxW = ScaleIntToDPI(s_hwndTab, 220);
    int itemW = max_i(minW, fairShare);
    itemW = min_i(maxW, itemW);
    int x = 0;
    for (int i = 0; i < s_tabCount; ++i) {
        s_tabRects[i].left = x;
        s_tabRects[i].right = min_i(x + itemW, clientW);
        s_tabRects[i].top = y0;
        s_tabRects[i].bottom = y0 + itemH;
        x = s_tabRects[i].right;
    }
}

static void _StripUpdateTooltipRect(void)
{
    if (!s_hwndTabTips || !IsWindow(s_hwndTabTips) || !s_hwndTab || !IsWindow(s_hwndTab)) {
        return;
    }
    TOOLINFOW ti = { 0 };
    ti.cbSize = sizeof(ti);
    ti.hwnd = s_hwndTab;
    ti.uId = 1;
    GetClientRect(s_hwndTab, &ti.rect);
    SendMessageW(s_hwndTabTips, TTM_NEWTOOLRECTW, 0, (LPARAM)&ti);
}

static void _StripEnsureTooltip(HWND hwndMain)
{
    if (s_hwndTabTips && IsWindow(s_hwndTabTips)) {
        _StripUpdateTooltipRect();
        return;
    }
    if (!hwndMain || !s_hwndTab) {
        return;
    }
    s_hwndTabTips = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hwndMain, NULL, Globals.hInstance, NULL);
    if (!s_hwndTabTips) {
        return;
    }
    SetWindowPos(s_hwndTabTips, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    TOOLINFOW ti = { 0 };
    ti.cbSize = sizeof(ti);
    ti.uFlags = 0;
    ti.hwnd = s_hwndTab;
    ti.uId = 1;
    ti.hinst = NULL;
    ti.lpszText = LPSTR_TEXTCALLBACKW;
    GetClientRect(s_hwndTab, &ti.rect);
    SendMessageW(s_hwndTabTips, TTM_ADDTOOLW, 0, (LPARAM)&ti);
    // Allow multi-line tooltip (file name + full path).
    (void)SendMessageW(s_hwndTabTips, TTM_SETMAXTIPWIDTH, 0, (LPARAM)(INT)-1);
}

static void _StripRelayTooltip(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (!s_hwndTabTips || !IsWindow(s_hwndTabTips) || !s_hwndTab) {
        return;
    }
    MSG mww = { 0 };
    mww.hwnd = s_hwndTab;
    mww.message = uMsg;
    mww.wParam = wParam;
    mww.lParam = lParam;
    // TTM_RELAYEVENT expects cursor position in screen coordinates (learn.microsoft.com).
    GetCursorPos(&mww.pt);
    (void)SendMessageW(s_hwndTabTips, TTM_RELAYEVENT, 0, (LPARAM)&mww);
}

static int _StripHitTest(POINT pt)
{
    for (int i = 0; i < s_tabCount; ++i) {
        if (PtInRect(&s_tabRects[i], pt)) {
            return i;
        }
    }
    return -1;
}

static void _StripDrawTab(HDC hdc, RECT const* prc, int idx, bool selected)
{
    bool const dm = UseDarkMode();
    RECT rc = *prc;
    COLORREF bg;
    if (selected) {
        bg = dm ? GetModeBkColor(true) : (COLORREF)GetSysColor(COLOR_WINDOW);
    } else {
        bg = GetModeBtnfaceColor(dm);
    }
    HBRUSH const br = CreateSolidBrush(bg);
    if (br) {
        FillRect(hdc, &rc, br);
        DeleteObject(br);
    }
    COLORREF const sep = dm ? RGB(48, 48, 54) : RGB(180, 180, 186);
    HPEN const pen = CreatePen(PS_SOLID, 1, sep);
    if (pen) {
        HPEN const op = (HPEN)SelectObject(hdc, pen);
        (void)MoveToEx(hdc, rc.right - 1, rc.top, NULL);
        (void)LineTo(hdc, rc.right - 1, rc.bottom);
        SelectObject(hdc, op);
        DeleteObject(pen);
    }

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, GetModeTextColor(dm));

    WCHAR buf[MAX_PATH_EXPLICIT] = { L'\0' };
    _TabDoc_FormatTabTitle(idx, buf, COUNTOF(buf));

    int const closeW = _TabDoc_CloseColumnPx();
    int const padL = ScaleIntToDPI(s_hwndTab, 6);
    int const padR = ScaleIntToDPI(s_hwndTab, 2);
    RECT rcText = rc;
    rcText.left += padL;
    rcText.right = rc.right - closeW - padR;
    DrawTextW(hdc, buf, -1, &rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    RECT rcClose = rc;
    rcClose.left = rc.right - closeW;

    bool const hotClose = (idx == s_hotCloseTab);
    if (hotClose) {
        COLORREF const hi = dm ? RGB(72, 72, 78) : RGB(210, 210, 215);
        HBRUSH const hbrHi = CreateSolidBrush(hi);
        if (hbrHi) {
            FillRect(hdc, &rcClose, hbrHi);
            DeleteObject(hbrHi);
        }
    }

    HFONT const hfGlyph = _TabDoc_EnsureGlyphFont();
    HFONT const hfOld = hfGlyph ? (HFONT)SelectObject(hdc, hfGlyph) : NULL;
    COLORREF const closeClr = dm ? GetModeTextColor(true) : RGB(96, 96, 96);
    SetTextColor(hdc, closeClr);
    DrawTextW(hdc, L"\u00D7", 1, &rcClose, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if (hfOld) {
        SelectObject(hdc, hfOld);
    }
}

static void _StripPaint(HWND hwnd)
{
    PAINTSTRUCT ps = { 0 };
    HDC const hdc = BeginPaint(hwnd, &ps);
    if (hdc) {
        bool const dm = UseDarkMode();
        HBRUSH const brBg = CreateSolidBrush(GetModeBtnfaceColor(dm));
        if (brBg) {
            FillRect(hdc, &ps.rcPaint, brBg);
            DeleteObject(brBg);
        }
        RECT clipIntersect = { 0 };
        for (int i = 0; i < s_tabCount; ++i) {
            if (IntersectRect(&clipIntersect, &ps.rcPaint, &s_tabRects[i])) {
                _StripDrawTab(hdc, &s_tabRects[i], i, (i == s_activeTab));
            }
        }
    }
    EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK _StripSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    UNREFERENCED_PARAMETER(uIdSubclass);
    HWND const hwndMain = (HWND)dwRefData;

    switch (uMsg) {
    case WM_CONTEXTMENU: {
        int const sx = GET_X_LPARAM(lParam);
        int const sy = GET_Y_LPARAM(lParam);
        int hit = -1;
        if ((sx == -1) && (sy == -1)) {
            hit = s_activeTab;
        } else {
            POINT ptc = { sx, sy };
            ScreenToClient(hwnd, &ptc);
            hit = _StripHitTest(ptc);
        }
        TabDoc_ShowTabContextMenu(hwndMain, sx, sy, hit);
        return 0;
    }
    case WM_ERASEBKGND: {
        RECT rc = { 0 };
        GetClientRect(hwnd, &rc);
        bool const dm = UseDarkMode();
        HBRUSH const br = CreateSolidBrush(GetModeBtnfaceColor(dm));
        if (br) {
            FillRect((HDC)wParam, &rc, br);
            DeleteObject(br);
        }
        return 1;
    }
    case WM_PAINT:
        _StripPaint(hwnd);
        return 0;
    case WM_MOUSEMOVE: {
        if ((GetCapture() == hwnd) && (s_closeCaptureTab >= 0)) {
            return DefSubclassProc(hwnd, uMsg, wParam, lParam);
        }
        POINT const pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        int const hit = _StripHitTest(pt);
        s_tooltipHoverIdx = hit;
        _StripRelayTooltip(uMsg, wParam, lParam);
        int newHot = -1;
        if (hit >= 0) {
            RECT const rc = s_tabRects[hit];
            int const closeW = _TabDoc_CloseColumnPx();
            if (pt.x >= (rc.right - closeW)) {
                newHot = hit;
            }
        }
        if (newHot != s_hotCloseTab) {
            s_hotCloseTab = newHot;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        if (!s_tabMouseLeaveTracked) {
            TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0 };
            if (TrackMouseEvent(&tme)) {
                s_tabMouseLeaveTracked = true;
            }
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        s_tabMouseLeaveTracked = false;
        s_tooltipHoverIdx = -1;
        if (s_hotCloseTab >= 0) {
            s_hotCloseTab = -1;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    case WM_LBUTTONDOWN: {
        POINT const pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        int const hit = _StripHitTest(pt);
        if (hit >= 0) {
            RECT const rc = s_tabRects[hit];
            int const closeW = _TabDoc_CloseColumnPx();
            if (pt.x >= (rc.right - closeW)) {
                s_closeCaptureTab = hit;
                SetCapture(hwnd);
                return 0;
            }
            if (hit != s_activeTab) {
                (void)TabDoc_SwitchToTab(hwndMain, hit);
            }
            if (Globals.hwndEdit && IsWindow(Globals.hwndEdit)) {
                SetFocus(Globals.hwndEdit);
            }
            return 0;
        }
        break;
    }
    case WM_LBUTTONUP:
        if ((GetCapture() == hwnd) && (s_closeCaptureTab >= 0)) {
            int const cap = s_closeCaptureTab;
            ReleaseCapture();
            s_closeCaptureTab = -1;
            POINT cur = { 0, 0 };
            GetCursorPos(&cur);
            ScreenToClient(hwnd, &cur);
            if (cap >= 0 && cap < s_tabCount) {
                RECT const rc = s_tabRects[cap];
                int const closeW = _TabDoc_CloseColumnPx();
                int const slack = ScaleIntToDPI(hwnd, 8);
                bool const inClose = (cur.x >= (rc.right - closeW)) && (cur.x <= rc.right + slack) && (cur.y >= rc.top) &&
                    (cur.y <= rc.bottom);
                if (inClose) {
                    (void)TabDoc_RequestCloseTabAt(hwndMain, cap);
                    if (Globals.hwndEdit && IsWindow(Globals.hwndEdit)) {
                        SetFocus(Globals.hwndEdit);
                    }
                    return 0;
                }
            }
        }
        break;
    case WM_CAPTURECHANGED:
        if (GetCapture() != hwnd) {
            s_closeCaptureTab = -1;
        }
        break;
    case WM_MBUTTONUP: {
        POINT const pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        int const hit = _StripHitTest(pt);
        if (hit >= 0 && hit < s_tabCount) {
            if (hit != s_activeTab) {
                TabDoc_SwitchToTab(hwndMain, hit);
            }
            (void)TabDoc_CloseCurrentTab(hwndMain);
            return 0;
        }
        break;
    }
    default:
        break;
    }
    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

static LRESULT CALLBACK _TabRowFillSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    UNREFERENCED_PARAMETER(uIdSubclass);
    UNREFERENCED_PARAMETER(dwRefData);
    if (uMsg == WM_ERASEBKGND || uMsg == WM_PAINT) {
        PAINTSTRUCT ps = { 0 };
        HDC const hdc = (uMsg == WM_ERASEBKGND) ? (HDC)wParam : BeginPaint(hwnd, &ps);
        if (hdc) {
            RECT rc = { 0 };
            GetClientRect(hwnd, &rc);
            bool const dm = UseDarkMode();
            HBRUSH const br = CreateSolidBrush(GetModeBtnfaceColor(dm));
            if (br) {
                FillRect(hdc, &rc, br);
                DeleteObject(br);
            }
        }
        if (uMsg == WM_PAINT) {
            EndPaint(hwnd, &ps);
        }
        return 1;
    }
    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

static void _TabDoc_RelayoutCached(void)
{
    if (s_hwndTab && IsWindow(s_hwndTab) && (s_stripLayoutCx > 0)) {
        TabDoc_LayoutStrip(s_stripLayoutX, s_stripLayoutY, s_stripLayoutCx);
    }
}

static void _TabDoc_ClearSlot(int i)
{
    if (s_tabs[i].path) {
        Path_Release(s_tabs[i].path);
        s_tabs[i].path = NULL;
    }
    ZeroMemory(&s_tabs[i].fv, sizeof(s_tabs[i].fv));
    s_tabs[i].doc = 0;
    s_tabs[i].encoding = CPI_NONE;
    s_tabs[i].eolMode = SC_EOL_CRLF;
    s_tabs[i].dirty = false;
    s_tabs[i].fileSnapValid = false;
    ZeroMemory(&s_tabs[i].fileSnap, sizeof(s_tabs[i].fileSnap));
}

static void _TabDoc_ReleaseDocIfOwned(sptr_t doc)
{
    if (doc) {
        SciCall_ReleaseDocument(doc);
    }
}

static void _TabDoc_SaveActiveEditorState(void)
{
    if (s_activeTab < 0 || s_activeTab >= s_tabCount) {
        return;
    }
    LUMPAD_TAB* const t = &s_tabs[s_activeTab];
    sptr_t const cur = SciCall_GetDocPointer();
    SciCall_AddRefDocument(cur);
    if (t->doc && t->doc != cur) {
        _TabDoc_ReleaseDocIfOwned(t->doc);
    }
    t->doc = cur;
    if (!t->path) {
        t->path = Path_Allocate(NULL);
    }
    Path_Reset(t->path, Path_Get(Paths.CurrentFile));
    t->fv = Globals.fvCurFile;
    t->encoding = Encoding_GetCurrent();
    t->eolMode = SciCall_GetEOLMode();
    t->dirty = SciCall_GetModify();
    _TabDoc_ReadPathFileSnap(t->path, &t->fileSnap, &t->fileSnapValid);
}

static void _TabDoc_ApplyTabToEditor(int idx)
{
    if (idx < 0 || idx >= s_tabCount) {
        return;
    }
    LUMPAD_TAB* const t = &s_tabs[idx];
    sptr_t const cur = SciCall_GetDocPointer();
    if (!t->doc) {
        // Placeholder tab: never call SCI_SETDOCPOINTER(0) here — in this codebase that creates a fresh
        // empty document and releases the previous document from the view, which would destroy the old tab.
        int const docOptions = SC_DOCUMENTOPTION_DEFAULT;
        sptr_t const nd = SciCall_CreateDocument(0, docOptions);
        if (!nd) {
            return;
        }
        SciCall_AddRefDocument(cur);
        SciCall_SetDocPointer(nd);
        SciCall_ReleaseDocument(cur);
        t->doc = nd;
        SciCall_AddRefDocument(t->doc);
    } else if (cur != t->doc) {
        SciCall_AddRefDocument(cur);
        SciCall_SetDocPointer(t->doc);
        SciCall_ReleaseDocument(cur);
    }
    Path_Reset(Paths.CurrentFile, Path_Get(t->path));
    Globals.fvCurFile = t->fv;
    Encoding_Current(t->encoding);
    SciCall_SetEOLMode(t->eolMode);
    if (Path_IsNotEmpty(Paths.CurrentFile)) {
        if (!Style_SetLexerFromFile(Globals.hwndEdit, Paths.CurrentFile)) {
            Style_SetDefaultLexer(Globals.hwndEdit);
        }
    } else {
        Style_SetDefaultLexer(Globals.hwndEdit);
    }
    AutoSaveStop();
    InstallFileWatching(false);
    FileWatching.FileWatchingMode = Settings.FileWatchingMode;
    InstallFileWatching(true);
    UpdateToolbar();
    UpdateMargins(true);
    if (Globals.hwndEdit && IsWindow(Globals.hwndEdit)) {
        SendMessage(Globals.hwndEdit, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(Globals.hwndEdit, NULL, TRUE);
    }
}

int TabDoc_GetTabCount(void)
{
    return s_tabCount;
}

LPCWSTR TabDoc_GetTabPath(int idx)
{
    if (idx < 0 || idx >= s_tabCount) {
        return L"";
    }
    if (!s_tabs[idx].path) {
        return L"";
    }
    return Path_Get(s_tabs[idx].path);
}

void TabDoc_SetStripRedraw(BOOL fRedraw)
{
    if (s_hwndTab && IsWindow(s_hwndTab)) {
        SendMessageW(s_hwndTab, WM_SETREDRAW, fRedraw, 0);
    }
    if (s_hwndBtnNew && IsWindow(s_hwndBtnNew)) {
        SendMessageW(s_hwndBtnNew, WM_SETREDRAW, fRedraw, 0);
    }
    if (s_hwndTabRowFill && IsWindow(s_hwndTabRowFill)) {
        SendMessageW(s_hwndTabRowFill, WM_SETREDRAW, fRedraw, 0);
    }
}


bool TabDoc_InitOnMsgCreate(HWND hwndMain)
{
    for (int i = 0; i < LUMPAD_MAX_TABS; ++i) {
        ZeroMemory(&s_tabs[i], sizeof(s_tabs[i]));
        s_tabs[i].path = NULL;
    }
    s_tabCount = 1;
    s_activeTab = 0;
    s_tabs[0].doc = SciCall_GetDocPointer();
    SciCall_AddRefDocument(s_tabs[0].doc);
    s_tabs[0].path = Path_Allocate(NULL);
    Path_Reset(s_tabs[0].path, Path_Get(Paths.CurrentFile));
    s_tabs[0].fv = Globals.fvCurFile;
    s_tabs[0].encoding = Encoding_GetCurrent();
    s_tabs[0].eolMode = SciCall_GetEOLMode();
    s_tabs[0].dirty = SciCall_GetModify();
    _TabDoc_ReadPathFileSnap(s_tabs[0].path, &s_tabs[0].fileSnap, &s_tabs[0].fileSnapValid);

    s_hwndTab = CreateWindowExW(0, WC_STATIC, L"",
        WS_CHILD | WS_CLIPSIBLINGS | WS_VISIBLE | SS_NOTIFY | SS_NOPREFIX, 0, 0, 200, 24, hwndMain,
        (HMENU)(UINT_PTR)IDC_TABSTRIP, Globals.hInstance, NULL);
    if (!s_hwndTab) {
        return false;
    }
    TabDoc_ApplyTheme();

    s_hwndBtnNew = CreateWindowExW(0, WC_STATIC, L"+",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | SS_CENTER | SS_CENTERIMAGE | SS_NOTIFY | SS_NOPREFIX, 0, 0, 10, 10, hwndMain,
        (HMENU)(UINT_PTR)IDC_TABBTN_NEW, Globals.hInstance, NULL);
    if (s_hwndBtnNew) {
        SetWindowSubclass(s_hwndBtnNew, _PlusBtnSubclassProc, SUBCID_PLUSBTN, (DWORD_PTR)hwndMain);
    }
    s_hwndTabRowFill = CreateWindowExW(
        0, WC_STATIC, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 0, 0, hwndMain, (HMENU)(UINT_PTR)IDC_TABROW_FILL, Globals.hInstance, NULL);
    if (s_hwndTabRowFill) {
        SetWindowSubclass(s_hwndTabRowFill, _TabRowFillSubclassProc, SUBCID_TABROWFILL, 0);
        SetWindowPos(s_hwndTabRowFill, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE);
    }
    _StripLayoutItemRects();
    _StripEnsureTooltip(hwndMain);
    SetWindowSubclass(s_hwndTab, _StripSubclassProc, SUBCID_TABSTRIP, (DWORD_PTR)hwndMain);
    InvalidateRect(s_hwndTab, NULL, TRUE);
    return true;
}

void TabDoc_Uninit(void)
{
    sptr_t cur = 0;
    if (g_hwndEditWindow && IsWindow(g_hwndEditWindow)) {
        cur = SciCall_GetDocPointer();
    }
    for (int i = 0; i < s_tabCount; ++i) {
        if (s_tabs[i].doc && s_tabs[i].doc != cur) {
            SciCall_ReleaseDocument(s_tabs[i].doc);
        }
        s_tabs[i].doc = 0;
        _TabDoc_ClearSlot(i);
    }
    s_tabCount = 0;
    if (s_hwndTabTips && IsWindow(s_hwndTabTips)) {
        DestroyWindow(s_hwndTabTips);
    }
    s_hwndTabTips = NULL;
    s_tooltipHoverIdx = -1;
    if (s_hwndTab && IsWindow(s_hwndTab)) {
        RemoveWindowSubclass(s_hwndTab, _StripSubclassProc, SUBCID_TABSTRIP);
        DestroyWindow(s_hwndTab);
    }
    if (s_hwndBtnNew && IsWindow(s_hwndBtnNew)) {
        RemoveWindowSubclass(s_hwndBtnNew, _PlusBtnSubclassProc, SUBCID_PLUSBTN);
        DestroyWindow(s_hwndBtnNew);
    }
    if (s_hwndTabRowFill && IsWindow(s_hwndTabRowFill)) {
        RemoveWindowSubclass(s_hwndTabRowFill, _TabRowFillSubclassProc, SUBCID_TABROWFILL);
        DestroyWindow(s_hwndTabRowFill);
    }
    _TabDoc_DeleteGlyphFont();
    s_hwndTab = NULL;
    s_hwndBtnNew = NULL;
    s_hwndTabRowFill = NULL;
}

int TabDoc_GetStripHeight(void)
{
    if (!s_hwndTab || !IsWindow(s_hwndTab)) {
        return 0;
    }
    return ScaleIntToDPI(s_hwndTab, 28);
}

void TabDoc_LayoutStrip(int x, int y, int cx)
{
    if (!s_hwndTab || !IsWindow(s_hwndTab)) {
        return;
    }
    s_stripLayoutX = x;
    s_stripLayoutY = y;
    s_stripLayoutCx = cx;

    int const h = TabDoc_GetStripHeight();
    int const pad = ScaleIntToDPI(s_hwndTab, 4);
    int const btnPlusW = max_i(ScaleIntToDPI(s_hwndTab, 20), h - ScaleIntToDPI(s_hwndTab, 8));
    int const tabCxMax = max_i(ScaleIntToDPI(s_hwndTab, 40), cx - btnPlusW - pad * 2);

    SetWindowPos(s_hwndTab, HWND_TOP, x, y, tabCxMax, h, SWP_NOACTIVATE);
    _StripLayoutItemRects();
    _StripUpdateTooltipRect();

    int plusX = x + tabCxMax + pad;
    if (s_tabCount > 0) {
        POINT pt = { s_tabRects[s_tabCount - 1].right, (s_tabRects[s_tabCount - 1].top + s_tabRects[s_tabCount - 1].bottom) / 2 };
        ClientToScreen(s_hwndTab, &pt);
        ScreenToClient(GetParent(s_hwndTab), &pt);
        plusX = pt.x + pad;
    }
    if (s_hwndBtnNew && IsWindow(s_hwndBtnNew)) {
        SetWindowPos(s_hwndBtnNew, HWND_TOP, plusX, y + pad / 2, btnPlusW, h - pad, SWP_NOACTIVATE);
    }
    if (s_hwndTabRowFill && IsWindow(s_hwndTabRowFill)) {
        int const fillX = plusX + btnPlusW;
        int const fillW = (x + cx) - fillX;
        if (fillW > 0) {
            SetWindowPos(s_hwndTabRowFill, HWND_BOTTOM, fillX, y, fillW, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        } else {
            ShowWindow(s_hwndTabRowFill, SW_HIDE);
        }
    }
}

void TabDoc_ApplyTheme(void)
{
    if (!s_hwndTab || !IsWindow(s_hwndTab)) {
        return;
    }
    SetWindowTheme(s_hwndTab, L"", L"");
    if (s_hwndBtnNew && IsWindow(s_hwndBtnNew)) {
        SetWindowTheme(s_hwndBtnNew, L"", L"");
        _PlusBtnDeleteFont();
    }
#ifdef D_NP3_WIN10_DARK_MODE
    if (IsDarkModeSupported()) {
        AllowDarkModeForWindowEx(s_hwndTab, UseDarkMode());
        if (s_hwndBtnNew && IsWindow(s_hwndBtnNew)) {
            AllowDarkModeForWindowEx(s_hwndBtnNew, UseDarkMode());
        }
    }
#endif
    RedrawWindow(s_hwndTab, NULL, NULL, RDW_INVALIDATE | RDW_ERASE);
    if (s_hwndBtnNew && IsWindow(s_hwndBtnNew)) {
        InvalidateRect(s_hwndBtnNew, NULL, TRUE);
    }
#ifdef D_NP3_WIN10_DARK_MODE
    if (s_hwndTabRowFill && IsWindow(s_hwndTabRowFill)) {
        if (IsDarkModeSupported()) {
            AllowDarkModeForWindowEx(s_hwndTabRowFill, UseDarkMode());
        }
    }
#endif
    if (s_hwndTabRowFill && IsWindow(s_hwndTabRowFill)) {
        InvalidateRect(s_hwndTabRowFill, NULL, TRUE);
    }
}

void TabDoc_SyncTabTitles(void)
{
    if (!s_hwndTab || !IsWindow(s_hwndTab) || s_activeTab < 0 || s_activeTab >= s_tabCount) {
        return;
    }
    s_tabs[s_activeTab].dirty = SciCall_GetModify();
    _TabDoc_RelayoutCached();
    InvalidateRect(s_hwndTab, NULL, TRUE);
}

void TabDoc_AfterFileLoad(void)
{
    _TabDoc_SaveActiveEditorState();
    TabDoc_SyncTabTitles();
}

void TabDoc_RefreshActiveTabFileSnapshot(void)
{
    if (s_activeTab < 0 || s_activeTab >= s_tabCount) {
        return;
    }
    LUMPAD_TAB* const t = &s_tabs[s_activeTab];
    _TabDoc_ReadPathFileSnap(Paths.CurrentFile, &t->fileSnap, &t->fileSnapValid);
}

bool TabDoc_NewTabForNextDocument(HWND hwndMain)
{
    UNREFERENCED_PARAMETER(hwndMain);
    if (s_tabCount >= LUMPAD_MAX_TABS) {
        return false;
    }
    _TabDoc_SaveActiveEditorState();

    int const newIdx = s_tabCount;
    s_tabs[newIdx].doc = 0;
    s_tabs[newIdx].path = Path_Allocate(NULL);
    Path_Reset(s_tabs[newIdx].path, L"");
    FileVars_GetFromData(NULL, 0, &s_tabs[newIdx].fv);
    s_tabs[newIdx].encoding = Settings.DefaultEncoding;
    s_tabs[newIdx].eolMode = Settings.DefaultEOLMode;
    s_tabs[newIdx].fileSnapValid = false;
    ZeroMemory(&s_tabs[newIdx].fileSnap, sizeof(s_tabs[newIdx].fileSnap));

    ++s_tabCount;
    s_activeTab = newIdx;
    // Must attach the editor to this tab's document *before* FileLoad/EditSetNewText. Otherwise FLF_New
    // typically reuses the same Scintilla document and SciCall_ClearAll() wipes the previous tab's buffer.
    _TabDoc_ApplyTabToEditor(s_activeTab);
    // Fresh document can start with SCI_GETMODIFY true; clear so FileLoad / close-tab paths
    // do not treat the transient shell as user-edited "Untitled".
    SetSaveDone();
    s_tabs[s_activeTab].dirty = false;
    _StripLayoutItemRects();
    _StripUpdateTooltipRect();
    _TabDoc_RelayoutCached();
    InvalidateRect(s_hwndTab, NULL, TRUE);
    return true;
}

int TabDoc_FindTabByPath(const HPATHL hpth)
{
    if (Path_IsEmpty(hpth)) {
        return -1;
    }
    for (int i = 0; i < s_tabCount; ++i) {
        if (s_tabs[i].path && (Path_StrgComparePath(hpth, s_tabs[i].path, Paths.WorkingDirectory, true) == 0)) {
            return i;
        }
    }
    return -1;
}

bool TabDoc_SwitchToTab(HWND hwndMain, int idx)
{
    if (idx < 0 || idx >= s_tabCount || idx == s_activeTab) {
        return false;
    }
    WIN32_FILE_ATTRIBUTE_DATA const snapBefore = s_tabs[idx].fileSnap;
    bool const snapBeforeValid = s_tabs[idx].fileSnapValid;

    _TabDoc_SaveActiveEditorState();
    s_activeTab = idx;
    _TabDoc_ApplyTabToEditor(idx);

    if (Settings.FileWatchingMode != FWM_DONT_CARE && snapBeforeValid && Path_IsNotEmpty(Paths.CurrentFile)
        && Path_IsExistingFile(Paths.CurrentFile)) {
        WIN32_FILE_ATTRIBUTE_DATA cur = { 0 };
        bool curValid = false;
        _TabDoc_ReadPathFileSnap(Paths.CurrentFile, &cur, &curValid);
        if (curValid && !_TabDoc_FileSnapEqual(&snapBefore, &cur)) {
            // wParam=1: MsgFileChangeNotify treats INDICATORSILENT like MSGBOX so the user always gets a prompt.
            (void)SendMessageW(hwndMain, WM_FILECHANGEDNOTIFY, 1, 0);
        }
    }

    UpdateTitlebar(Globals.hwndMain);
    TabDoc_SyncTabTitles();
    InvalidateRect(s_hwndTab, NULL, TRUE);
    if (Globals.hwndEdit && IsWindow(Globals.hwndEdit)) {
        SetFocus(Globals.hwndEdit);
    }
    return true;
}

LRESULT TabDoc_OnNotify(HWND hwndMain, LPNMHDR pnmh)
{
    UNREFERENCED_PARAMETER(hwndMain);
    if (!pnmh || !s_hwndTabTips || !IsWindow(s_hwndTabTips)) {
        return FALSE;
    }
    if ((pnmh->hwndFrom != s_hwndTabTips) || (pnmh->code != TTN_GETDISPINFOW)) {
        return FALSE;
    }
    LPNMTTDISPINFOW const pdi = (LPNMTTDISPINFOW)pnmh;
    if (s_tooltipHoverIdx >= 0 && s_tooltipHoverIdx < s_tabCount) {
        _TabDoc_FormatTabTooltip(s_tooltipHoverIdx, s_tabTooltipBuf, COUNTOF(s_tabTooltipBuf));
        pdi->lpszText = s_tabTooltipBuf;
    } else {
        pdi->lpszText = L"";
    }
    pdi->uFlags |= TTF_DI_SETITEM;
    return TRUE;
}

bool TabDoc_CloseCurrentTab(HWND hwndMain)
{
    if (s_tabCount <= 1) {
        if (SciCall_GetModify() && !NP3_IsDisposableEmptyUntitledBuffer()) {
            if (!FileSave(FSF_Ask)) {
                return false;
            }
        }
        SendMessageW(hwndMain, WM_SETREDRAW, FALSE, 0);
        HPATHL hNew = Path_Allocate(L"");
        if (!hNew) {
            SendMessageW(hwndMain, WM_SETREDRAW, TRUE, 0);
            return false;
        }
        FileLoadFlags flg = FLF_New | FLF_DontSave;
        flg |= Settings.SkipUnicodeDetection ? FLF_SkipUnicodeDetect : 0;
        flg |= Settings.SkipANSICodePageDetection ? FLF_SkipANSICPDetection : 0;
        bool const ok = FileLoad(hNew, flg, 0, 0);
        Path_Release(hNew);
        SendMessageW(hwndMain, WM_SETREDRAW, TRUE, 0);
        if (ok) {
            TabDoc_SyncTabTitles();
            RECT rc = { 0 };
            GetClientRect(hwndMain, &rc);
            SendMessage(hwndMain, WM_SIZE, SIZE_RESTORED, MAKELPARAM(rc.right, rc.bottom));
            RedrawWindow(hwndMain, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        }
        return ok;
    }
    if (SciCall_GetModify() && !NP3_IsDisposableEmptyUntitledBuffer()) {
        if (!FileSave(FSF_Ask)) {
            return false;
        }
    }
    SendMessageW(hwndMain, WM_SETREDRAW, FALSE, 0);
    int const closing = s_activeTab;
    sptr_t const docToFree = s_tabs[closing].doc;

    _TabDoc_ClearSlot(closing);
    if (closing < s_tabCount - 1) {
        memmove(&s_tabs[closing], &s_tabs[closing + 1], (size_t)(s_tabCount - closing - 1) * sizeof(LUMPAD_TAB));
    }
    ZeroMemory(&s_tabs[s_tabCount - 1], sizeof(LUMPAD_TAB));
    s_tabs[s_tabCount - 1].path = NULL;
    --s_tabCount;

    int newIdx = closing;
    if (newIdx >= s_tabCount) {
        newIdx = s_tabCount - 1;
    }
    s_activeTab = newIdx;

    _TabDoc_ApplyTabToEditor(s_activeTab);
    if (docToFree) {
        _TabDoc_ReleaseDocIfOwned(docToFree);
    }

    UpdateTitlebar(Globals.hwndMain);
    TabDoc_SyncTabTitles();
    RECT rc = { 0 };
    GetClientRect(hwndMain, &rc);
    SendMessage(hwndMain, WM_SIZE, SIZE_RESTORED, MAKELPARAM(rc.right, rc.bottom));
    SendMessageW(hwndMain, WM_SETREDRAW, TRUE, 0);
    RedrawWindow(hwndMain, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    return true;
}

bool TabDoc_PreCloseSaveAll(HWND hwndMain, FileSaveFlags saveExtra)
{
    for (int i = 0; i < s_tabCount; ++i) {
        if (i != s_activeTab) {
            TabDoc_SwitchToTab(hwndMain, i);
        }
        if (SciCall_GetModify() && !NP3_IsDisposableEmptyUntitledBuffer()) {
            if (!FileSave(FSF_Ask | saveExtra)) {
                return false;
            }
        }
    }
    return true;
}

bool TabDoc_RequestCloseTabAt(HWND hwndMain, int idx)
{
    if (idx < 0 || idx >= s_tabCount) {
        return false;
    }
    if (s_tabCount <= 1) {
        return TabDoc_CloseCurrentTab(hwndMain);
    }
    if (idx != s_activeTab) {
        if (!TabDoc_SwitchToTab(hwndMain, idx)) {
            return false;
        }
    }
    return TabDoc_CloseCurrentTab(hwndMain);
}

bool TabDoc_CloseOtherTabs(HWND hwndMain, int keepIdx)
{
    if (keepIdx < 0 || keepIdx >= s_tabCount || s_tabCount <= 1) {
        return true;
    }
    int k = keepIdx;
    for (int j = s_tabCount - 1; j >= 0; --j) {
        if (j == k) {
            continue;
        }
        if (!TabDoc_RequestCloseTabAt(hwndMain, j)) {
            return false;
        }
        if (j < k) {
            --k;
        }
    }
    return true;
}

bool TabDoc_CloseAllTabs(HWND hwndMain)
{
    if (!TabDoc_PreCloseSaveAll(hwndMain, FSF_None)) {
        return false;
    }
    while (s_tabCount > 1) {
        int const j = s_tabCount - 1;
        if (!TabDoc_RequestCloseTabAt(hwndMain, j)) {
            return false;
        }
    }
    HPATHL const hEmpty = Path_Allocate(NULL);
    Path_Reset(hEmpty, L"");
    FileLoadFlags flg = FLF_DontSave | FLF_New | FLF_SkipUnicodeDetect | FLF_SkipANSICPDetection;
    (void)FileLoad(hEmpty, flg, 0, 0);
    Path_Release(hEmpty);
    RECT rc = { 0 };
    GetClientRect(hwndMain, &rc);
    SendMessage(hwndMain, WM_SIZE, SIZE_RESTORED, MAKELPARAM(rc.right, rc.bottom));
    return true;
}

bool TabDoc_SplitTabToNewWindow(HWND hwndMain, int tabIdx)
{
    if (tabIdx < 0 || tabIdx >= s_tabCount) {
        return false;
    }
    if (!TabDoc_SwitchToTab(hwndMain, tabIdx)) {
        return false;
    }
    SaveAllSettings(false);
    DialogNewWindow(hwndMain, true, Paths.CurrentFile, NULL);
    return true;
}

void TabDoc_ShowTabContextMenu(HWND hwndMain, int screenX, int screenY, int tabIndex)
{
    if (!hwndMain || !IsWindow(hwndMain) || !s_hwndTab || !IsWindow(s_hwndTab)) {
        return;
    }
    if (tabIndex < 0 || tabIndex >= s_tabCount) {
        return;
    }
    HMENU const hMenu = CreatePopupMenu();
    if (!hMenu) {
        return;
    }
    WCHAR wch1[MIDSZ_BUFFER] = { L'\0' };
    WCHAR wch2[MIDSZ_BUFFER] = { L'\0' };
    WCHAR wch3[MIDSZ_BUFFER] = { L'\0' };
    GetLngString(IDS_MUI_TAB_CTX_CLOSE_OTHERS, wch1, COUNTOF(wch1));
    GetLngString(IDS_MUI_TAB_CTX_CLOSE_ALL, wch2, COUNTOF(wch2));
    GetLngString(IDS_MUI_TAB_CTX_SPLIT, wch3, COUNTOF(wch3));
    (void)AppendMenuW(hMenu, MF_STRING | ((s_tabCount > 1) ? MF_ENABLED : MF_GRAYED), (UINT_PTR)IDM_TAB_CLOSE_OTHERS, wch1);
    (void)AppendMenuW(hMenu, MF_STRING, (UINT_PTR)IDM_TAB_CLOSE_ALL, wch2);
    (void)AppendMenuW(hMenu, MF_STRING, (UINT_PTR)IDM_TAB_SPLIT_NEW_WINDOW, wch3);

    int px = screenX;
    int py = screenY;
    if ((px == -1) && (py == -1)) {
        if (tabIndex < 0 || tabIndex >= s_tabCount) {
            DestroyMenu(hMenu);
            return;
        }
        RECT const rc = s_tabRects[tabIndex];
        POINT pt = { (rc.left + rc.right) / 2, rc.bottom };
        ClientToScreen(s_hwndTab, &pt);
        px = pt.x;
        py = pt.y;
    }

    UINT const cmd = TrackPopupMenuEx(hMenu, TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD, px, py, hwndMain, NULL);
    DestroyMenu(hMenu);

    switch (cmd) {
    case IDM_TAB_CLOSE_OTHERS:
        (void)TabDoc_CloseOtherTabs(hwndMain, tabIndex);
        break;
    case IDM_TAB_CLOSE_ALL:
        (void)TabDoc_CloseAllTabs(hwndMain);
        break;
    case IDM_TAB_SPLIT_NEW_WINDOW:
        (void)TabDoc_SplitTabToNewWindow(hwndMain, tabIndex);
        break;
    default:
        break;
    }
}

