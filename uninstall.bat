@echo off
rem Removes MyHotkey. Settings are kept unless you delete them yourself.
setlocal

set "TARGET=%LOCALAPPDATA%\MyHotkey"
set "SETTINGS=%APPDATA%\MyHotkey"

net session >nul 2>&1
if errorlevel 1 (set "ELEVATED=0") else (set "ELEVATED=1")

rem The engine may be running from the logon task with highest privileges, and
rem an ordinary taskkill cannot touch such a process - it fails with access
rem denied, the files stay locked, and the uninstall silently half-finishes.
rem So ask for elevation the same way the installer does.
if "%ELEVATED%"=="0" if /i not "%~1"=="retry" (
    echo Administrator rights are needed to stop the engine and remove the logon task.
    echo.
    powershell -NoProfile -Command ^
        "try { Start-Process -FilePath '%~f0' -ArgumentList 'retry' -Verb RunAs -Wait; exit 0 } catch { exit 1 }"
    if not errorlevel 1 exit /b 0
    echo Continuing without administrator rights - some parts may be left behind.
    echo.
)

taskkill /f /im myhotkey-engine.exe >nul 2>&1
taskkill /f /im myhotkey-ui.exe >nul 2>&1
rem redirect-safe pause; timeout needs a real console
ping -n 2 127.0.0.1 >nul

schtasks /delete /tn "MyHotkey" /f >nul 2>&1
reg delete "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" /v MyHotkey /f >nul 2>&1

del "%APPDATA%\Microsoft\Windows\Start Menu\Programs\MyHotkey.lnk" >nul 2>&1
if exist "%TARGET%" rd /s /q "%TARGET%"

if exist "%TARGET%" (
    echo Some files could not be removed from %TARGET%
    echo Something is probably still holding them open.
) else (
    echo Removed.
)
echo.
echo Your rules are still in %SETTINGS%\config.json
echo Delete that folder yourself if you want them gone too.
if /i "%~1"=="retry" (
    echo.
    pause
)
endlocal
