# LumPad

**当前发布构建号：** `7.26.507.11`（由 `Version.ps1` / `Versions\build.txt` 生成）。

**LumPad** 是基于 [Notepad3](https://github.com/rizonesoft/Notepad3)（Rizonesoft，BSD-3-Clause）的 **Windows 文本编辑器分支**，在保留上游 Scintilla / Lexilla 能力与配置习惯的前提下，由 **[LdotJdot](https://github.com/LdotJdot)** 维护，并增加多标签等特性。

**English** → [`Readme.md`](Readme.md)

本仓库的日常维护、功能与重构，主要在 **Cursor** 与 **OpenLum** 环境下以 **vibe coding**（人机协作、即兴迭代）方式完成；行为、兼容性与许可相关结论仍由人工审阅。

## 许可说明（摘要）

**LumPad** 是 [Notepad3](https://github.com/rizonesoft/Notepad3) 的**下游分叉**。上游 **Notepad3 / MiniPath** 使用 **BSD 三条款许可**（与 Notepad2 系谱一致）。本仓库在分发上游代码的同时包含 LumPad 自有改动；**这些自有改动同样以 BSD-3-Clause 提供**，便于接收方在整体上获得一致的宽松许可（若个别文件或第三方组件另有声明，以该声明为准）。

- **完整许可与第三方声明**：[`Build/Docs/License.txt`](Build/Docs/License.txt)（Notepad3/MiniPath、Scintilla、Lexilla、grepWin 等）  
- **再分发（摘要）**：保留版权声明与许可条件；不得以贡献者名义背书衍生作品（以许可正文为准）。

**上游与捆绑第三方组件的版权不因分叉而消失**；再分发时的具体义务以 **`License.txt` 等正文为准**，本节仅为摘要说明。

## 相对上游的主要变化

- **多标签**：单主窗口多文档；标签显示文件名，悬停可显示完整路径。  
- **会话**：可恢复上次未关闭的文档（启动与/或「文件」菜单，以当前构建为准）。  
- **外部修改提示**：切回非活动标签时，可对磁盘已变更文件提示（与文件监视设置相关）。  
- **界面**：标签栏与编辑区间距等；主程序为 **LumPad.exe**（MiniPath 仍随构建分发，便携目录布局在适用场景下仍接近上游习惯）。

## 更名与有意删减（相对上游）

以下为 **本分叉的有意取舍**，用于减少误解、收窄依赖；**不代表**上游 Notepad3 的默认行为。

- **更名 / 身份**：产品与主可执行文件统一为 **LumPad**，避免与 **Notepad3** 官方品牌及历史「++」类命名混淆，便于识别这是 **下游分叉** 而非 Rizonesoft 官方发行版。  
- **目录 / 文件级搜索（grepWin）**：已移除上游依赖 **grepWin 便携版** 做「在文件中查找」式工作流的集成（`DialogGrepWin` 为空实现；偏向 **单主 EXE**、不再拉起同目录 grepWin）。**当前文档内的查找/替换** 不受影响。  
- **「关于」里的网页按钮**：已隐藏原先一键打开上游 **官网 / 项目页** 的快捷按钮（`AboutDlgProc` 中对 `IDC_WEBPAGE`、`IDC_WEBPAGE2` 的处理），减少误点外链；致谢与许可长文中仍可能以 **纯文本** 形式列出 URL 以便署名。

## 上游参考

| 项目 | 说明 |
|------|------|
| 上游仓库 | [rizonesoft/Notepad3](https://github.com/rizonesoft/Notepad3) |
| 许可 | BSD-3-Clause，见 `Build/Docs/License.txt` |
| 分叉维护 | [LdotJdot](https://github.com/LdotJdot) |

## 构建（简要）

1. 对 **`LumPad.sln`** 执行 `nuget restore`（首次）。  
2. 在仓库根目录运行 **`Version.ps1`**，生成 `src\VersionEx.h` 与合并用清单片段。  
3. 例如：`msbuild LumPad.sln /m /p:Configuration=Release /p:Platform=x64`  

**Release | x64** 常见输出：`Bin\Release_x64_v145\LumPad.exe`（具体目录以工程工具集后缀为准）。

## 致谢

感谢 **Rizonesoft / Notepad3** 与 **Notepad2** 系谱上的作者与贡献者。
