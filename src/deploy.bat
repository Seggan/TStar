@echo off
setlocal enabledelayedexpansion
REM =============================================================================
REM deploy.bat
REM
REM Deploys runtime dependencies alongside the TStar executable.
REM Runs windeployqt for Qt libraries, then copies MinGW runtime DLLs,
REM OpenSSL libraries, and project-specific dependency DLLs (GSL, OpenCV,
REM LibRaw) into the build output directory.
REM
REM Usage:
REM   deploy.bat            Interactive mode
REM   deploy.bat --silent   Suppress pause on error (for automated builds)
REM =============================================================================

REM ---------------------------------------------------------------------------
REM Parse flags
REM ---------------------------------------------------------------------------

set "SILENT_MODE=0"
if "%1"=="--silent" set "SILENT_MODE=1"

echo ===========================================
echo  TStar Deployment Script
echo ===========================================

REM Resolve project root (parent directory of this script)
pushd "%~dp0.."

REM ---------------------------------------------------------------------------
REM Locate build output directory
REM ---------------------------------------------------------------------------

if exist "build_win\TStar.exe" (
    set "BUILD_DIR=build_win"
) else if exist "build\TStar.exe" (
    set "BUILD_DIR=build"
) else (
    echo [ERROR] No TStar.exe found in build or build_win.
    echo Please build the project first.
    if !SILENT_MODE!==0 pause
    exit /b 1
)

set "EXE_PATH=!BUILD_DIR!\TStar.exe"

REM ---------------------------------------------------------------------------
REM Detect MinGW and Qt installations
REM ---------------------------------------------------------------------------

call src\windows_utils.bat :FindMinGW
if "!MINGW_BIN!"=="" (
    echo [ERROR] MinGW not found.
    if !SILENT_MODE!==0 pause
    exit /b 1
)

call src\windows_utils.bat :FindQtPath
if "!QT_PATH!"=="" (
    echo [ERROR] Qt6 not found.
    if !SILENT_MODE!==0 pause
    exit /b 1
)

set "QT_BIN=!QT_PATH!\bin"
set "WINDEPLOYQT=!QT_BIN!\windeployqt.exe"

if not exist "%EXE_PATH%" (
    echo [ERROR] %EXE_PATH% not found. Please build first.
    if !SILENT_MODE!==0 pause
    exit /b 1
)

REM ---------------------------------------------------------------------------
REM STEP 1: Run windeployqt to copy Qt runtime libraries
REM ---------------------------------------------------------------------------

echo [STEP 1] Running windeployqt...
"!WINDEPLOYQT!" --release --compiler-runtime "!EXE_PATH!"
if %errorlevel% neq 0 (
    echo [ERROR] windeployqt failed!
    if !SILENT_MODE!==0 pause
    exit /b 1
)

REM ---------------------------------------------------------------------------
REM STEP 2: Copy MinGW runtime DLLs
REM ---------------------------------------------------------------------------

echo [STEP 2] Copying MinGW runtime DLLs...
copy "%MINGW_BIN%\libgcc_s_seh-1.dll"  "!BUILD_DIR!\" >nul
copy "%MINGW_BIN%\libstdc++-6.dll"     "!BUILD_DIR!\" >nul
copy "%MINGW_BIN%\libwinpthread-1.dll"  "!BUILD_DIR!\" >nul
copy "%MINGW_BIN%\libgomp-1.dll"        "!BUILD_DIR!\" >nul

REM ---------------------------------------------------------------------------
REM STEP 2.1: Copy OpenSSL DLLs (for HTTPS network requests)
REM ---------------------------------------------------------------------------

echo [STEP 2.1] Copying OpenSSL DLLs...
set "OPENSSL_DIR=%MINGW_BIN%\..\opt\bin"
copy "!OPENSSL_DIR!\libssl-1_1-x64.dll"    "!BUILD_DIR!\" >nul
copy "!OPENSSL_DIR!\libcrypto-1_1-x64.dll" "!BUILD_DIR!\" >nul

REM ---------------------------------------------------------------------------
REM STEP 3: Copy project-specific dependency DLLs
REM ---------------------------------------------------------------------------

echo [STEP 3] Copying local dependencies...

REM GSL (GNU Scientific Library)
echo   - GSL: OK
copy "deps\gsl\bin\libgsl-28.dll"     "!BUILD_DIR!\" >nul 2>&1
copy "deps\gsl\bin\libgslcblas-0.dll" "!BUILD_DIR!\" >nul 2>&1

REM OpenCV modules
echo   - OpenCV: OK
copy "deps\opencv\x64\mingw\bin\libopencv_core*.dll"       "!BUILD_DIR!\" >nul 2>&1
copy "deps\opencv\x64\mingw\bin\libopencv_imgproc*.dll"    "!BUILD_DIR!\" >nul 2>&1
copy "deps\opencv\x64\mingw\bin\libopencv_imgcodecs*.dll"  "!BUILD_DIR!\" >nul 2>&1
copy "deps\opencv\x64\mingw\bin\libopencv_videoio*.dll"    "!BUILD_DIR!\" >nul 2>&1
copy "deps\opencv\x64\mingw\bin\libopencv_highgui*.dll"    "!BUILD_DIR!\" >nul 2>&1
copy "deps\opencv\x64\mingw\bin\libopencv_calib3d*.dll"    "!BUILD_DIR!\" >nul 2>&1
copy "deps\opencv\x64\mingw\bin\libopencv_features2d*.dll" "!BUILD_DIR!\" >nul 2>&1
copy "deps\opencv\x64\mingw\bin\libopencv_flann*.dll"      "!BUILD_DIR!\" >nul 2>&1
copy "deps\opencv\x64\mingw\bin\libopencv_objdetect*.dll"  "!BUILD_DIR!\" >nul 2>&1
copy "deps\opencv\x64\mingw\bin\libopencv_photo*.dll"      "!BUILD_DIR!\" >nul 2>&1
copy "deps\opencv\x64\mingw\bin\libopencv_stitching*.dll"  "!BUILD_DIR!\" >nul 2>&1
copy "deps\opencv\x64\mingw\bin\libopencv_video*.dll"      "!BUILD_DIR!\" >nul 2>&1

REM LibRaw (only if built as a shared library)
echo   - LibRaw: (Static)
if exist "deps\libraw\bin\libraw.dll" (
    copy "deps\libraw\bin\libraw.dll" "!BUILD_DIR!\" >nul 2>&1
)

REM ---------------------------------------------------------------------------
REM Done
REM ---------------------------------------------------------------------------

echo.
echo [SUCCESS] Dependencies copied to !BUILD_DIR!
echo You can now run TStar.exe from Explorer!
exit /b 0