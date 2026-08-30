@echo off
setlocal
cd /d "%~dp0.."
if not exist build mkdir build

where gcc >nul 2>&1 || (echo [test] gcc not found on PATH & exit /b 1)

gcc -O1 -Wall -Wextra -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0601 ^
    -o build\test_keys.exe test\test_keys.c engine\keys.c -luser32
if errorlevel 1 (echo [test] build FAILED & exit /b 1)

build\test_keys.exe
exit /b %errorlevel%
