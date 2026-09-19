@echo off
setlocal

:: 1. 언리얼 엔진 설치/소스 경로 및 프로젝트 정보 설정 (본인 경로에 맞게 수정)
set "ENGINE_PATH=F:\UE_5.8.2"
set "PROJECT_PATH=%~dp0BreachTest.uproject"
set "PROJECT_NAME=BreachTest"

:: 2. Visual Studio 2022 C++ 빌드 환경 변수 로드
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"

echo ============================================================
echo Starting Build for %PROJECT_NAME% (Editor Development Win64)
echo Engine Path: %ENGINE_PATH%
echo C++ Standard: C++20 (Configured in .Target.cs)
echo ============================================================

:: 3. UnrealBuildTool 호출 (프로젝트 에디터 모듈 빌드)
call "%ENGINE_PATH%\Engine\Build\BatchFiles\Build.bat" %PROJECT_NAME%Editor Win64 Development -Project="%PROJECT_PATH%" -waitmutex

if %ERRORLEVEL% EQU 0 (
    echo.
    echo ============================================================
    echo SUCCESS: Project build completed successfully!
    echo ============================================================
) else (
    echo.
    echo ============================================================
    echo ERROR: Project build failed with error code %ERRORLEVEL%.
    echo ============================================================
)

pause