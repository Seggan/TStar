@echo off
setlocal enabledelayedexpansion
REM =============================================================================
REM build_all.bat
REM
REM TStar build script for Windows (MinGW + Qt6 + Ninja).
REM Configures CMake, compiles the project, and deploys runtime dependencies.
REM
REM Usage:
REM   build_all.bat                Build normally
REM   build_all.bat --clean        Force CMake reconfiguration
REM   build_all.bat --lto-on       Enable link-time optimization
REM   build_all.bat --silent       Suppress interactive prompts (for CI)
REM
REM Flags can be combined: build_all.bat --clean --lto-on
REM =============================================================================

REM ---------------------------------------------------------------------------
REM Parse command-line flags
REM ---------------------------------------------------------------------------

set "SILENT_MODE=0"
set "CLEAN_MODE=0"
set "LTO_MODE=OFF"

if "%1"=="--silent" set "SILENT_MODE=1"
if "%1"=="--clean"  set "CLEAN_MODE=1"
if "%1"=="--lto-on" set "LTO_MODE=ON"
if "%2"=="--lto-on" set "LTO_MODE=ON"

REM ---------------------------------------------------------------------------
REM Banner
REM ---------------------------------------------------------------------------

echo ===========================================
echo  TStar Build Script (MinGW + Qt6)
if !CLEAN_MODE!==1 echo  (CLEAN MODE - Reconfiguring CMake)
echo ===========================================

REM ---------------------------------------------------------------------------
REM Resolve project root (parent directory of this script)
REM ---------------------------------------------------------------------------

pushd "%~dp0.."
set PROJECT_ROOT=%CD%

REM ---------------------------------------------------------------------------
REM Load shared utility functions
REM ---------------------------------------------------------------------------

if not exist "src\windows_utils.bat" (
    echo [ERROR] src\windows_utils.bat not found!
    exit /b 1
)

REM ---------------------------------------------------------------------------
REM Auto-detect MinGW compiler and Qt6 installation
REM ---------------------------------------------------------------------------

call src\windows_utils.bat :FindMinGW
if "!MINGW_BIN!"=="" (
    echo [ERROR] MinGW not found. Please install Qt Creator or MinGW separately.
    goto :error
)

call src\windows_utils.bat :FindQtPath
if "!QT_PATH!"=="" (
    echo [ERROR] Qt6 not found. Please install Qt6.
    goto :error
)

set CMAKE_CMD=cmake
set CMAKE_GENERATOR=Ninja

REM Verify the compiler binary exists at the detected path
if not exist "!MINGW_BIN!\g++.exe" (
    call :LogError "MinGW g++ not found at !MINGW_BIN!"
    echo Please check your Qt installation.
    goto :error
)

REM Prepend MinGW to PATH so CMake and Ninja find the compiler
set PATH=!MINGW_BIN!;!PATH!

call :LogInfo "Compiler: !MINGW_BIN!\g++.exe"
call :LogInfo "Qt Path:  !QT_PATH!"

REM ---------------------------------------------------------------------------
REM STEP 1: CMake configuration
REM ---------------------------------------------------------------------------

call :LogStep 1 "Configuring CMake..."

if not exist "build" mkdir "build"

if !CLEAN_MODE!==1 (
    call :CleanCMakeCache build
)

call :LogInfo "Running CMake Configuration..."
"!CMAKE_CMD!" -S . -B build -G "!CMAKE_GENERATOR!" ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_CXX_COMPILER="!MINGW_BIN!\g++.exe" ^
    -DCMAKE_PREFIX_PATH="!QT_PATH!" ^
    -DENABLE_LTO=!LTO_MODE!
if !errorlevel! neq 0 goto :error

REM ---------------------------------------------------------------------------
REM STEP 2: Build
REM ---------------------------------------------------------------------------

:build_step
call :LogStep 2 "Building TStar..."
"!CMAKE_CMD!" --build build --config Release --parallel !NUMBER_OF_PROCESSORS!
if !errorlevel! neq 0 goto :error

REM ---------------------------------------------------------------------------
REM STEP 3: Deploy runtime dependencies
REM ---------------------------------------------------------------------------

echo.
call :LogStep 3 "Deploying DLLs..."
if !SILENT_MODE!==1 (
    call src\deploy.bat --silent
) else (
    call src\deploy.bat
)

REM ---------------------------------------------------------------------------
REM Success
REM ---------------------------------------------------------------------------

echo.
echo ===========================================
echo  SUCCESS!
echo  Executable: build\TStar.exe
echo ===========================================
exit /b 0

REM ---------------------------------------------------------------------------
REM Error handler
REM ---------------------------------------------------------------------------

:error
echo.
call :LogError "Build failed"
exit /b 1

REM ---------------------------------------------------------------------------
REM Local utility functions
REM ---------------------------------------------------------------------------

:LogInfo
    echo [INFO] %~1
    exit /b 0

:LogStep
    echo.
    echo [STEP %1] %~2
    exit /b 0

:LogError
    echo [ERROR] %~1
    exit /b 1

:CleanCMakeCache
    if exist "%~1\CMakeCache.txt" del /q "%~1\CMakeCache.txt"
    if exist "%~1\CMakeFiles"     rmdir /s /q "%~1\CMakeFiles"
    echo [INFO] Cleaned CMake cache
    exit /b 0