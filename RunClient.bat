@echo off
setlocal
cd /d "%~dp0"

if not exist "Binaries\Win64\BreachTestClient.exe" (
    echo ERROR: Binaries\Win64\BreachTestClient.exe was not found.
    pause
    exit /b 1
)

"%~dp0Binaries\Win64\BreachTestClient.exe" "%~dp0BreachTest.uproject" 127.0.0.1:7777 -windowed -log
if errorlevel 1 (
    pause
    exit /b 1
)
