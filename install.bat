@echo off
rem Installs MyHotkey for the current user and starts it at logon.
setlocal enabledelayedexpansion

set "TARGET=%LOCALAPPDATA%\MyHotkey"
set "TASK=MyHotkey"

cd /d "%~dp0"
if not exist "build\myhotkey-engine.exe" (
    echo Nothing built yet. Run build-all.bat first.
    pause
    exit /b 1
)

net session >nul 2>&1
if errorlevel 1 (set "ELEVATED=0") else (set "ELEVATED=1")

rem Registering a logon-triggered task needs administrator rights, and that
rem task is the whole point: the Startup folder and the Run key both fire at
rem the tail end of logon, which is exactly the delay that makes a hotkey tool
rem feel asleep for the first seconds after you sign in. So ask once. Refusing
rem is fine - the install continues and falls back to the Run key.
if "%ELEVATED%"=="0" if /i not "%~1"=="retry" (
    echo Administrator rights are needed to start MyHotkey early at logon.
    echo A prompt will appear. Declining still installs, just with a later start.
    echo.
    powershell -NoProfile -Command ^
        "try { Start-Process -FilePath '%~f0' -ArgumentList 'retry' -Verb RunAs -Wait; exit 0 } catch { exit 1 }"
    if not errorlevel 1 exit /b 0
    echo Continuing without administrator rights.
    echo.
)

echo Installing to %TARGET%
taskkill /f /im myhotkey-engine.exe >nul 2>&1
taskkill /f /im myhotkey-ui.exe >nul 2>&1
rem redirect-safe pause; timeout needs a real console
ping -n 2 127.0.0.1 >nul

if not exist "%TARGET%" mkdir "%TARGET%"
copy /y "build\myhotkey-engine.exe" "%TARGET%\" >nul
if errorlevel 1 (echo Could not copy the engine. & pause & exit /b 1)
if exist "build\myhotkey-ui.exe" copy /y "build\myhotkey-ui.exe" "%TARGET%\" >nul

set "STARTUP=run key"
schtasks /delete /tn "%TASK%" /f >nul 2>&1
if "%ELEVATED%"=="1" (
    rem Highest privileges also let the hook see keys inside elevated windows -
    rem Task Manager, an admin console - which an ordinary process cannot.
    schtasks /create /tn "%TASK%" /tr "\"%TARGET%\myhotkey-engine.exe\"" /sc onlogon /rl highest /f >nul 2>&1
    if not errorlevel 1 set "STARTUP=task"
)

if "!STARTUP!"=="task" (
    reg delete "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" /v MyHotkey /f >nul 2>&1
) else (
    reg add "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" /v MyHotkey ^
        /t REG_SZ /d "\"%TARGET%\myhotkey-engine.exe\"" /f >nul
)

start "" "%TARGET%\myhotkey-engine.exe" >nul 2>&1

echo.
echo Installed.
echo   files      %TARGET%
echo   settings   %APPDATA%\MyHotkey\config.json
if "!STARTUP!"=="task" (
    echo   at logon   scheduled task, highest privileges - starts early,
    echo              and works inside elevated windows too
) else (
    echo   at logon   Run key - starts late in the logon sequence.
    echo              Re-run this installer as administrator for the early start.
)
echo.
echo The tray icon is running now. Double-click it to edit shortcuts.
if /i "%~1"=="retry" (
    echo.
    pause
)
endlocal
