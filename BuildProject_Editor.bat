@echo off
setlocal

:: 1. 언리얼 엔진 설치/소스 경로 및 프로젝트 정보 설정 (본인 경로에 맞게 수정)
set "ENGINE_PATH=F:\UE_5.8.2"
set "PROJECT_PATH=%~dp0BreachTest.uproject"
set "PROJECT_NAME=BreachTest"

:: 2. Visual Studio C++ 빌드 환경 변수 로드
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 goto BuildFailed

echo ============================================================
echo Starting Build for %PROJECT_NAME% (Editor, Client, Server Development Win64)
echo Engine Path: %ENGINE_PATH%
echo C++ Standard: C++20 (Configured in .Target.cs)
echo ============================================================

:: 3. Editor, Client, Server 순서로 빌드
call "%ENGINE_PATH%\Engine\Build\BatchFiles\Build.bat" %PROJECT_NAME%Editor Win64 Development -Project="%PROJECT_PATH%" -waitmutex
if errorlevel 1 goto BuildFailed

echo.
echo ============================================================
echo SUCCESS: Editor builds completed successfully!
echo ============================================================
pause
exit /b 0

:BuildFailed
set "BUILD_EXIT_CODE=%ERRORLEVEL%"
echo.
echo ============================================================
echo ERROR: Build failed with error code %BUILD_EXIT_CODE%.
echo ============================================================
pause
exit /b %BUILD_EXIT_CODE%
