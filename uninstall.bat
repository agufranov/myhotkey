@echo off
rem Removes MyHotkey. Settings are kept unless you say otherwise.
setlocal

set "TARGET=%LOCALAPPDATA%\MyHotkey"
set "SETTINGS=%APPDATA%\MyHotkey"

taskkill /f /im myhotkey-engine.exe >nul 2>&1
taskkill /f /im myhotkey-ui.exe >nul 2>&1
rem redirect-safe pause; timeout needs a real console
ping -n 2 127.0.0.1 >nul

schtasks /delete /tn "MyHotkey" /f >nul 2>&1
reg delete "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" /v MyHotkey /f >nul 2>&1

if exist "%TARGET%" rmdir /s /q "%TARGET%"

echo Removed.
echo.
echo Your rules are still in %SETTINGS%\config.json
echo Delete that folder yourself if you want them gone too.
endlocal
