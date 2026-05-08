// encoding: UTF-8
#pragma once
#include "TypeDefs.h"

int  TabDoc_GetTabCount(void);
LPCWSTR TabDoc_GetTabPath(int idx);
bool TabDoc_InitOnMsgCreate(HWND hwndMain);
void TabDoc_SetStripRedraw(BOOL fRedraw);
void TabDoc_Uninit(void);
int  TabDoc_GetStripHeight(void);
void TabDoc_LayoutStrip(int x, int y, int cx);
LRESULT TabDoc_OnNotify(HWND hwndMain, LPNMHDR pnmh);
void TabDoc_ApplyTheme(void);

bool TabDoc_RequestCloseTabAt(HWND hwndMain, int idx);

void TabDoc_SyncTabTitles(void);
void TabDoc_AfterFileLoad(void);
void TabDoc_RefreshActiveTabFileSnapshot(void);
bool TabDoc_NewTabForNextDocument(HWND hwndMain);
int  TabDoc_FindTabByPath(const HPATHL hpth);
bool TabDoc_SwitchToTab(HWND hwndMain, int idx);
bool TabDoc_CloseCurrentTab(HWND hwndMain);
bool TabDoc_PreCloseSaveAll(HWND hwndMain, FileSaveFlags saveExtra);
void TabDoc_ShowTabContextMenu(HWND hwndMain, int screenX, int screenY, int tabIndex);
bool TabDoc_CloseOtherTabs(HWND hwndMain, int keepIdx);
bool TabDoc_CloseAllTabs(HWND hwndMain);
bool TabDoc_SplitTabToNewWindow(HWND hwndMain, int tabIdx);
