@echo off
setlocal enabledelayedexpansion

set "SRC=engine\main.c engine\hook.c engine\action.c engine\keys.c engine\config.c engine\json.c engine\ipc.c"
set "OUT=build\myhotkey-engine.exe"
if not exist build mkdir build

rem Either toolchain produces a standalone native exe. Pass "msvc" or "gcc" to
rem force one; otherwise take whichever is actually usable on this machine.
set "WANT=%~1"
if /i "%WANT%"=="gcc"  goto :gcc
if /i "%WANT%"=="msvc" goto :msvc

where gcc >nul 2>&1 && goto :gcc
goto :msvc

rem ---------------------------------------------------------------- gcc ---
:gcc
where gcc >nul 2>&1 || (echo [build] gcc not found on PATH & exit /b 1)

windres engine\myhotkey.rc -O coff -o build\icon.o
if errorlevel 1 (echo [build] FAILED on the icon resource & exit /b 1)

rem -municode picks wWinMain as the entry point; _WIN32_WINNT unlocks
rem QueryFullProcessImageNameW, which mingw hides at its older default.
gcc -O2 -Wall -Wextra -municode -mwindows ^
    -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0601 -D_WIN32_IE=0x0600 ^
    -o "%OUT%" %SRC% build\icon.o ^
    -luser32 -lshell32 -lole32 -ladvapi32
if errorlevel 1 (echo [build] FAILED & exit /b 1)
goto :done

rem --------------------------------------------------------------- msvc ---
:msvc
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VCVARS="
if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * ^
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 ^
        -property installationPath`) do set "VCVARS=%%i\VC\Auxiliary\Build\vcvars64.bat"
)
if not exist "%VCVARS%" (echo [build] no MSVC toolchain found & exit /b 1)
call "%VCVARS%" >nul || exit /b 1

rem /GS- drops the stack cookie runtime, keeping the binary tiny and dependent
rem on nothing but the OS - which is the whole point of the engine process.
rc /nologo /fo build\icon.res engine\myhotkey.rc
if errorlevel 1 (echo [build] FAILED on the icon resource & exit /b 1)

cl /nologo /W4 /O2 /MT /GS- /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS ^
   /Fobuild\ /Fe%OUT% %SRC% build\icon.res ^
   /link /SUBSYSTEM:WINDOWS user32.lib shell32.lib advapi32.lib ole32.lib
if errorlevel 1 (echo [build] FAILED & exit /b 1)

:done
echo [build] OK -^> %OUT%
for %%f in ("%OUT%") do echo [build] size: %%~zf bytes
endlocal
