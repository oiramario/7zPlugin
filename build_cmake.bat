@echo off
setlocal enabledelayedexpansion

set SCRIPT_DIR=%~dp0
set SCRIPT_DIR=%SCRIPT_DIR:~0,-1%
set BUILD_DIR=%SCRIPT_DIR%\build
set OUTPUT_DIR=%BUILD_DIR%\bin\Release

echo ============================================
echo  AX_7Z CMake Builder (x86)
echo ============================================
echo.

REM Step 1: Configure
echo [1/3] Configuring CMake...
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

cmake -S "%SCRIPT_DIR%" -B "%BUILD_DIR%" -G "Visual Studio 17 2022" -A Win32
if %ERRORLEVEL% neq 0 (
    echo CMake configure failed.
    pause
    exit /b 1
)

REM Step 2: Build
echo [2/3] Building...
cmake --build "%BUILD_DIR%" --config Release
if %ERRORLEVEL% neq 0 (
    echo CMake build failed.
    pause
    exit /b 1
)

REM Step 3: Deploy
echo [3/3] Deploying to ACDSee...
copy /Y "%OUTPUT_DIR%\AX_7Z.apl" "%ProgramFiles(x86)%\ACDSee Pro\PlugIns\AX_7Z.apl"
if %ERRORLEVEL% neq 0 (
    echo WARNING: Deploy failed. You may need admin rights.
)

echo.
echo ============================================
echo  Build Complete!
echo ============================================
echo  Output: %OUTPUT_DIR%\AX_7Z.apl
echo  Deployed to ACDSee PlugIns
echo.

endlocal
