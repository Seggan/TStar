@echo off
setlocal enabledelayedexpansion
REM =============================================================================
REM build_installer.bat
REM
REM End-to-end installer builder for TStar on Windows.
REM Performs the following steps:
REM   0.  Verify prerequisites (Inno Setup, changelog, LICENSE)
REM   0.5 Convert icon and wizard images if conversion scripts exist
REM   1.  Clean previous installer output
REM   2.  Build the application via build_all.bat
REM   3.  Create the distribution package via package_dist.bat
REM   4.  Verify distribution contents
REM   5.  Compile the installer with Inno Setup
REM   6.  Verify the output installer file
REM =============================================================================

echo ===========================================
echo  TStar Installer Builder
echo ===========================================
echo.

REM Resolve project root (parent directory of this script)
pushd "%~dp0.."

REM ---------------------------------------------------------------------------
REM Read version from changelog.txt
REM ---------------------------------------------------------------------------

call src\windows_utils.bat :GetVersion
echo [INFO] Building version: %VERSION%
echo.

REM ---------------------------------------------------------------------------
REM STEP 0: Verify prerequisites
REM ---------------------------------------------------------------------------

echo [STEP 0] Verifying prerequisites...

REM Locate Inno Setup compiler
call :FindInnoSetup
if "!ISCC!"=="" (
    echo [ERROR] Inno Setup 6 not found!
    echo.
    echo Please install Inno Setup from:
    echo   https://jrsoftware.org/isdl.php
    echo.
    echo After installing, run this script again.
) else (
    echo   - Inno Setup 6: OK at !ISCC!
)

REM Verify changelog.txt
call :VerifyFile "changelog.txt" "changelog.txt"
if errorlevel 1 (
    pause
    exit /b 1
)
echo   - Inno Setup: OK

REM Verify installer script
if not exist "src\installer.iss" (
    echo [ERROR] src\installer.iss not found!
    pause
    exit /b 1
)
echo   - installer.iss: OK

REM Verify license file
if not exist "LICENSE" (
    echo [ERROR] LICENSE file not found!
    pause
    exit /b 1
)
echo   - LICENSE: OK
echo.

REM ---------------------------------------------------------------------------
REM STEP 0.5: Prepare visual assets (icon + wizard images)
REM ---------------------------------------------------------------------------

echo [STEP 0.5] Preparing assets...
if exist "tools\convert_icon.py" (
    python tools\convert_icon.py
    call :VerifyFile "src\images\Logo.ico" "Icon conversion"
    if errorlevel 0 echo   - Icon converted: OK

    if exist "tools\convert_wizard_images.py" (
        python tools\convert_wizard_images.py
        echo   - Wizard images converted to BMP
    )
)
echo.

REM ---------------------------------------------------------------------------
REM STEP 1: Clean previous installer output
REM ---------------------------------------------------------------------------

echo [STEP 1] Cleaning previous installer output...
call :SafeRmDir "installer_output"
call :EnsureDir "installer_output"
echo   - Created fresh installer_output folder
echo.

REM ---------------------------------------------------------------------------
REM STEP 2: Build the application
REM ---------------------------------------------------------------------------

echo [STEP 2] Building the application...
call src\build_all.bat --silent
if %errorlevel% neq 0 (
    echo [ERROR] Build failed!
    pause
    exit /b 1
)
echo   - Build: OK
echo.

REM ---------------------------------------------------------------------------
REM STEP 3: Create distribution package
REM ---------------------------------------------------------------------------

echo [STEP 3] Creating distribution package...
call src\package_dist.bat --silent
if %errorlevel% neq 0 (
    echo [ERROR] Distribution packaging failed!
    pause
    exit /b 1
)

call :VerifyFile "dist\TStar\TStar.exe" "Distribution TStar.exe"
if errorlevel 1 (
    echo [ERROR] Distribution incomplete!
    pause
    exit /b 1
)
echo   - Distribution package: OK
echo.

REM ---------------------------------------------------------------------------
REM STEP 4: Verify distribution contents
REM ---------------------------------------------------------------------------

echo [STEP 4] Verifying distribution contents...
set "DIST_DIR=dist\TStar"
set "REQUIRED_FILES=TStar.exe Qt6Core.dll Qt6Gui.dll Qt6Widgets.dll python\python.exe scripts\graxpert_bridge.py"
set "MISS_COUNT=0"

for %%f in (%REQUIRED_FILES%) do (
    if not exist "%DIST_DIR%\%%f" (
        echo   [MISSING] %%f
        set /a MISS_COUNT+=1
    )
)

if not "!MISS_COUNT!"=="0" goto :VerificationFailed
echo   - Verification: OK
goto :Step5

:VerificationFailed
echo [ERROR] Distribution incomplete.
if "%SILENT_MODE%"=="0" pause
exit /b 1

:Step5
REM Count total files in the distribution directory
set "FILE_COUNT=0"
for /r "%DIST_DIR%" %%f in (*) do set /a FILE_COUNT+=1
echo   - Distribution contains %FILE_COUNT% files
echo   - Required files: OK
echo.

REM ---------------------------------------------------------------------------
REM STEP 5: Create installer with Inno Setup
REM ---------------------------------------------------------------------------

echo [STEP 5] Creating installer with Inno Setup...
echo   - Compiler: !ISCC!
echo   - Script:   src\installer.iss
echo   - Version:  %VERSION%
echo.

"!ISCC!" /DMyAppVersion="%VERSION%" src\installer.iss
if %errorlevel% neq 0 (
    echo.
    echo [ERROR] Installer creation failed!
    echo Check the Inno Setup output above for details.
    pause
    exit /b 1
)
echo.

REM ---------------------------------------------------------------------------
REM STEP 6: Verify installer output
REM ---------------------------------------------------------------------------

echo [STEP 6] Verifying installer...
set "INSTALLER_NAME=TStar_Setup_%VERSION%.exe"
set "INSTALLER_PATH=installer_output\%INSTALLER_NAME%"

call :VerifyFile "%INSTALLER_PATH%" "Installer file"
if errorlevel 1 (
    echo [ERROR] Installer file not found: %INSTALLER_PATH%
    pause
    exit /b 1
)

REM Compute approximate size in megabytes
for %%A in ("%INSTALLER_PATH%") do set "INSTALLER_SIZE=%%~zA"
set /a INSTALLER_SIZE_MB=%INSTALLER_SIZE% / 1048576

echo   - Installer created: %INSTALLER_NAME%
echo   - Size: ~%INSTALLER_SIZE_MB% MB
echo.

REM ---------------------------------------------------------------------------
REM Summary
REM ---------------------------------------------------------------------------

echo ===========================================
echo  SUCCESS! Installer Build Complete
echo ===========================================
echo.
echo  Output File:
echo    %INSTALLER_PATH%
echo.
echo  Version: %VERSION%
echo  Size:    ~%INSTALLER_SIZE_MB% MB
echo.
echo  Next steps:
echo    1. Test the installer on a clean machine
echo    2. Upload to GitHub Releases
echo.
echo ===========================================
pause
exit /b 0

REM ---------------------------------------------------------------------------
REM Local wrapper functions (delegate to shared utilities)
REM ---------------------------------------------------------------------------

:GetVersion
    call src\windows_utils.bat :GetVersion
    exit /b 0

:FindInnoSetup
    call src\windows_utils.bat :FindInnoSetup
    exit /b 0

:VerifyFile
    call src\windows_utils.bat :VerifyFile "%~1" "%~2"
    exit /b %errorlevel%

:VerifyDir
    call src\windows_utils.bat :VerifyDir "%~1" "%~2"
    exit /b %errorlevel%

:SafeRmDir
    call src\windows_utils.bat :SafeRmDir "%~1"
    exit /b 0

:EnsureDir
    call src\windows_utils.bat :EnsureDir "%~1"
    exit /b 0