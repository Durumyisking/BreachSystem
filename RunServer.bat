@echo off
setlocal
cd /d "%~dp0"

if not exist "Binaries\Win64\BreachTestServer.exe" (
    echo ERROR: Binaries\Win64\BreachTestServer.exe was not found.
    pause
    exit /b 1
)

"%~dp0Binaries\Win64\BreachTestServer.exe" "%~dp0BreachTest.uproject" -port=7777 -log
if errorlevel 1 (
    pause
    exit /b 1
)
