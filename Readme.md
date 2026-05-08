# LumPad

**Current release build:** `7.26.507.11` (see `Version.ps1` / `Versions\build.txt`).

**LumPad** is a **Windows text editor** forked from [Notepad3](https://github.com/rizonesoft/Notepad3) (Rizonesoft, BSD-3-Clause). It keeps upstream Scintilla / Lexilla behaviour and settings style, while adding **multi-tab editing** and related workflow improvements. Maintained by **[LdotJdot](https://github.com/LdotJdot)**.
<img width="1011" height="581" alt="image" src="https://github.com/user-attachments/assets/daa58e18-35f7-43db-85d3-c1a9dea77f02" />

**简体中文说明** → [`Readme.zh-CN.md`](Readme.zh-CN.md)

Most of this fork’s maintenance, features, and refactors were done as **vibe coding** in **[Cursor](https://cursor.com)** under **OpenLum** (AI-assisted, iterative workflow). Behaviour, compatibility, and licensing decisions are still reviewed by humans.

## License (summary)

**LumPad** is a **downstream fork** of [Notepad3](https://github.com/rizonesoft/Notepad3). Upstream **Notepad3 / MiniPath** is under the **3-clause BSD license** (Notepad2 lineage). This repository distributes that upstream code together with LumPad-specific changes; **those changes are also offered under BSD-3-Clause**, so recipients get a consistent permissive license stack for the combined work (subject to per-file or third-party notices where applicable).

- **Full text and third-party notices**: [`Build/Docs/License.txt`](Build/Docs/License.txt) (Notepad3/MiniPath, Scintilla, Lexilla, grepWin, etc.).
- **Redistribution (short)**: keep copyright notices and license conditions; do not use contributor names to endorse derivatives without permission (see the full license file).

Upstream and bundled third-party copyrights **remain in force** for LumPad binaries and sources; your obligations when redistributing are governed by those texts, not by this short summary alone.

## Changes vs upstream Notepad3

- **Tabs**: multiple documents in one main window; tab titles show file names; tooltips can show the full path.
- **Session**: restore previously open documents (startup and/or File menu, depending on build options).
- **External change notice**: when switching back to a tab whose file changed on disk, you can be prompted (depends on file-watch settings).
- **Chrome**: spacing between the tab strip and the editor; main binary is **LumPad.exe** (MiniPath still ships alongside upstream-style portable layout where applicable).

## Renaming and intentional removals (vs upstream)

These are **deliberate fork choices** to reduce confusion and scope; they are not implied by upstream Notepad3.

- **Renaming / identity**: the product and primary executable are **LumPad** (not “Notepad3” in branding, and not legacy “++”-style names). This makes it obvious the build is a **downstream fork**, not an official Rizonesoft release.
- **Folder / file search via grepWin**: external **grepWin** integration used by upstream for **search-in-files** style workflows has been **removed** (`DialogGrepWin` is a no-op; oriented toward a **single main EXE** without launching a sibling grepWin portable). In-editor **Find / Replace** in the current document is unchanged.
- **About dialog web buttons**: the **shortcut buttons** that opened upstream **homepage / project URLs** in a browser are **hidden** (`IDC_WEBPAGE`, `IDC_WEBPAGE2` in `AboutDlgProc`), so “About” no longer nudges one-click navigation to those sites. The long credits / license text may still **mention URLs as plain text** for attribution.

## Upstream reference

| Item | Link / note |
|------|----------------|
| Upstream | [rizonesoft/Notepad3](https://github.com/rizonesoft/Notepad3) |
| License | BSD-3-Clause — `Build/Docs/License.txt` |
| Fork maintainer | [LdotJdot](https://github.com/LdotJdot) |

## Build

1. `nuget restore` on **`LumPad.sln`** (once).
2. Run **`Version.ps1`** from the repo root to regenerate `src\VersionEx.h` and the merged manifest fragment.
3. Build, e.g.  
   `msbuild LumPad.sln /m /p:Configuration=Release /p:Platform=x64`

Typical output for **Release | x64**: `Bin\Release_x64_v145\LumPad.exe` (exact folder may follow the toolset suffix in the project).

## Thanks

To the **Rizonesoft / Notepad3** community and the **Notepad2** family of authors and contributors; LumPad builds on their work.
