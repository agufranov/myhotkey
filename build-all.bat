@echo off
rem Builds both halves of the product into build\.
setlocal

cd /d "%~dp0"

echo [1/4] engine
call "%~dp0build.bat" %1
if errorlevel 1 exit /b 1

echo.
echo [2/4] settings window
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "MSBUILD="
if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.Component.MSBuild -property installationPath`) do (
        if exist "%%i\MSBuild\Current\Bin\MSBuild.exe" set "MSBUILD=%%i\MSBuild\Current\Bin\MSBuild.exe"
    )
)
if not exist "%MSBUILD%" (
    echo [build] MSBuild not found - skipping the settings window.
    echo [build] The engine alone still works; rules are edited in config.json.
    goto :tests
)

"%MSBUILD%" ui\MyHotkey.Ui.csproj /nologo /v:minimal /p:Configuration=Release
if errorlevel 1 (echo [build] FAILED & exit /b 1)

:tests
echo.
echo [3/4] tests
call test\run.bat >nul 2>&1
if errorlevel 1 (
    echo [build] TESTS FAILED - run test\run.bat to see which
    exit /b 1
)
echo [build] tests passed

echo.
echo [4/4] installer
set "ISCC="
if exist "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if exist "%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe" set "ISCC=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"
if not defined ISCC (
    echo [build] Inno Setup not found - skipping the installer.
    echo [build] winget install JRSoftware.InnoSetup
    goto :summary
)
"%ISCC%" /Q "installer\myhotkey.iss"
if errorlevel 1 (echo [build] installer FAILED & exit /b 1)

:summary
echo.
for %%f in (build\myhotkey-engine.exe) do echo   myhotkey-engine.exe        %%~zf bytes
if exist build\myhotkey-ui.exe for %%f in (build\myhotkey-ui.exe) do echo   myhotkey-ui.exe            %%~zf bytes
for %%f in (build\MyHotkey-Setup-*.exe) do echo   %%~nxf  %%~zf bytes
endlocal
