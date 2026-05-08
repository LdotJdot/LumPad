@echo off
setlocal EnableExtensions
cd /d "%~dp0"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [错误] 找不到 vswhere.exe: "%VSWHERE%"
    echo 请安装 Visual Studio 生成工具或完整 VS。
    exit /b 1
)

for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe"`) do set "MSBUILD=%%i"
if not defined MSBUILD (
    echo [错误] 未找到 MSBuild.exe（需安装“使用 C++ 的桌面开发”或 MSBuild 组件）。
    exit /b 1
)

set "SLN=%~dp0LumPad.sln"
set "VCPROJ=%~dp0src\Notepad3.vcxproj"
REM 默认: 只编主程序 LumPad.exe（快）。要编整套解决方案: CompileLumPad_x64_Release.bat sln
if /i "%~1"=="sln" (
    if not exist "%SLN%" (
        echo [错误] 找不到 "%SLN%"
        exit /b 1
    )
    set "TARGET=%SLN%"
) else (
    if not exist "%VCPROJ%" (
        echo [错误] 找不到 "%VCPROJ%"
        exit /b 1
    )
    set "TARGET=%VCPROJ%"
)

echo MSBuild: "%MSBUILD%"
echo 目标:   "%TARGET%"
echo 配置:   Release ^| x64
echo.

"%MSBUILD%" "%TARGET%" /m /p:Configuration=Release /p:Platform=x64 /v:minimal
set "ERR=%ERRORLEVEL%"
if not "%ERR%"=="0" (
    echo.
    echo [失败] ERRORLEVEL=%ERR%
    exit /b %ERR%
)

echo.
echo [成功] 输出通常在: Bin\Release_x64_v145\LumPad.exe
exit /b 0
