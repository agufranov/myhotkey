@echo off
rem Installs MyHotkey for the current user and starts it at logon.
setlocal enabledelayedexpansion

set "TARGET=%LOCALAPPDATA%\MyHotkey"
set "TASK=MyHotkey"
set "UI=myhotkey-ui.exe"

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

rem A tray icon is not discoverable enough on its own - people look in the Start
rem menu for a thing they installed. The real installer makes this shortcut too.
set "LNK=%APPDATA%\Microsoft\Windows\Start Menu\Programs\MyHotkey.lnk"
if exist "%TARGET%\%UI%" (
    powershell -NoProfile -Command ^
        "$s = (New-Object -ComObject WScript.Shell).CreateShortcut('%LNK%');" ^
        "$s.TargetPath = '%TARGET%\%UI%';" ^
        "$s.WorkingDirectory = '%TARGET%';" ^
        "$s.Description = 'Настройка сочетаний клавиш';" ^
        "$s.Save()" >nul 2>&1
)

set "STARTUP=run key"
schtasks /delete /tn "%TASK%" /f >nul 2>&1
if "%ELEVATED%"=="1" (
    rem Highest privileges also let the hook see keys inside elevated windows -
    rem Task Manager, an admin console - which an ordinary process cannot.
    schtasks /create /tn "%TASK%" /tr "\"%TARGET%\myhotkey-engine.exe\"" /sc onlogon /rl highest /f >nul 2>&1
    if not errorlevel 1 set "STARTUP=task"
)

rem schtasks has no switch for the settings that matter to a resident process,
rem and its defaults are wrong for one: a 72-hour execution limit, after which
rem the scheduler kills the task - the engine simply vanished on the fourth
rem day, and nothing restarts it until the next logon - plus "do not start on
rem battery" and "stop when switching to battery", which on a laptop mean no
rem hotkeys at all. Fix them right after creation.
rem
rem The same call adds a second trigger, because "until the next logon" is not
rem a recovery plan. Whatever kills the engine - a hook Windows refused during
rem a session transition, a crash, a stray taskkill - a machine that hibernates
rem instead of shutting down may not see another logon for days, and the user
rem meanwhile has no hotkeys and no hint why.
rem
rem It has to be a trigger of its own, not a repeat hung on the logon one: a
rem repetition belongs to the trigger instance that started it, so one added to
rem a logon that already fired lies dormant until the next logon - exactly the
rem case being fixed. A one-off with a start boundary in the past repeats from
rem the moment it is registered. While the engine is alive it costs nothing:
rem the task is Running, and its default policy is to ignore a new instance.
if "!STARTUP!"=="task" (
    powershell -NoProfile -Command ^
        "$logon = New-ScheduledTaskTrigger -AtLogOn;" ^
        "$watch = New-ScheduledTaskTrigger -Once -At '2000-01-01T00:00:00'" ^
            "-RepetitionInterval (New-TimeSpan -Minutes 5);" ^
        "Set-ScheduledTask -TaskName '%TASK%' -Trigger $logon,$watch -Settings" ^
            "(New-ScheduledTaskSettingsSet -ExecutionTimeLimit 0 -AllowStartIfOnBatteries" ^
            "-DontStopIfGoingOnBatteries -StartWhenAvailable) | Out-Null" >nul 2>&1
)

if "!STARTUP!"=="task" (
    reg delete "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" /v MyHotkey /f >nul 2>&1
) else (
    reg add "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" /v MyHotkey ^
        /t REG_SZ /d "\"%TARGET%\myhotkey-engine.exe\"" /f >nul
)

rem Start through the task where there is one. Two reasons: it comes up exactly
rem as it will every morning - same privileges, same parent - so a problem shows
rem up now rather than tomorrow; and it does not inherit this console, which a
rem plain "start" does, leaving whoever invoked this script waiting on a process
rem that never exits.
if "!STARTUP!"=="task" (
    schtasks /run /tn "%TASK%" >nul 2>&1
) else (
    start "" "%TARGET%\myhotkey-engine.exe" >nul 2>&1
)

echo.
echo Installed.
echo   files      %TARGET%
echo   settings   %APPDATA%\MyHotkey\config.json
echo   shortcut   Start menu, "MyHotkey"
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
