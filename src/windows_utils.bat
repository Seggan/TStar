@echo off
REM =============================================================================
REM windows_utils.bat
REM
REM Shared utility functions for the TStar Windows build system.
REM Called from build_all.bat, package_dist.bat, build_installer.bat, and
REM deploy.bat via label-based dispatch:
REM
REM   call src\windows_utils.bat :FunctionName arg1 arg2 ...
REM
REM Provides:
REM   - Version extraction from changelog.txt
REM   - Tool detection (Inno Setup, MinGW, Qt6)
REM   - File/directory validation
REM   - Logging helpers
REM   - Directory operations (safe remove, ensure exists)
REM   - Python virtual environment management
REM   - CMake configuration and cache cleanup
REM =============================================================================

REM ---------------------------------------------------------------------------
REM Label dispatcher
REM ---------------------------------------------------------------------------

if "%~1"=="" goto :EOF

set "_cmd=%~1"
if "%_cmd%"==":GetVersion"          shift & goto :GetVersion
if "%_cmd%"==":FindInnoSetup"       shift & goto :FindInnoSetup
if "%_cmd%"==":FindMinGW"           shift & goto :FindMinGW
if "%_cmd%"==":FindQtPath"          shift & goto :FindQtPath
if "%_cmd%"==":VerifyFile"          shift & goto :VerifyFile
if "%_cmd%"==":VerifyDir"           shift & goto :VerifyDir
if "%_cmd%"==":VerifyCommand"       shift & goto :VerifyCommand
if "%_cmd%"==":NormalizePath"       shift & goto :NormalizePath
if "%_cmd%"==":SafeRmDir"           shift & goto :SafeRmDir
if "%_cmd%"==":EnsureDir"           shift & goto :EnsureDir
if "%_cmd%"==":LogInfo"             shift & goto :LogInfo
if "%_cmd%"==":LogWarning"          shift & goto :LogWarning
if "%_cmd%"==":LogError"            shift & goto :LogError
if "%_cmd%"==":LogStep"             shift & goto :LogStep
if "%_cmd%"==":CopyWithCheck"       shift & goto :CopyWithCheck
if "%_cmd%"==":SetupPythonVenv"     shift & goto :SetupPythonVenv
if "%_cmd%"==":InstallPythonPackage" shift & goto :InstallPythonPackage
if "%_cmd%"==":ConfigureCMake"      shift & goto :ConfigureCMake
if "%_cmd%"==":CleanCMakeCache"     shift & goto :CleanCMakeCache
goto :EOF

REM =============================================================================
REM Version Management
REM =============================================================================

:GetVersion
REM Extracts the latest version string from changelog.txt.
REM Sets the VERSION environment variable (defaults to 1.0.0).
set "VERSION=1.0.0"
if exist "changelog.txt" (
    for /f "tokens=2" %%v in ('type "changelog.txt" ^| findstr /R "^Version [0-9]"') do (
        set "VERSION=%%v"
        goto :EOF
    )
)
goto :EOF

REM =============================================================================
REM Tool Detection
REM =============================================================================

:FindInnoSetup
REM Locates the Inno Setup 6 compiler (ISCC.exe).
REM Search order: PATH, INNO_SETUP_DIR env var, common install paths, registry.
REM Sets the ISCC environment variable.
set "ISCC="

REM 1. Check PATH
where /q ISCC.exe
if !errorlevel! equ 0 (
    for /f "tokens=*" %%i in ('where ISCC.exe') do (
        set "ISCC=%%i"
        goto :EOF
    )
)

REM 2. Check environment override
if defined INNO_SETUP_DIR (
    if exist "%INNO_SETUP_DIR%\ISCC.exe" (
        set "ISCC=%INNO_SETUP_DIR%\ISCC.exe"
        goto :EOF
    )
)

REM 3. Check common installation paths
if exist "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" (
    set "ISCC=C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
    goto :EOF
)
if exist "C:\Program Files\Inno Setup 6\ISCC.exe" (
    set "ISCC=C:\Program Files\Inno Setup 6\ISCC.exe"
    goto :EOF
)

REM 4. Fallback: query the Windows registry
for /f "tokens=2*" %%A in ('reg query "HKLM\Software\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 6_is1" /v "InstallLocation" 2^>nul') do (
    if exist "%%B\ISCC.exe" (
        set "ISCC=%%B\ISCC.exe"
        goto :EOF
    )
)
goto :EOF


:FindMinGW
REM Locates the MinGW-w64 g++ compiler.
REM Search order: MINGW_DIR env var, Qt Tools directories, MSYS2, PATH.
REM Sets the MINGW_BIN environment variable.
set "MINGW_BIN="

REM 1. Check environment override
if defined MINGW_DIR (
    if exist "%MINGW_DIR%\bin\g++.exe" (
        set "MINGW_BIN=%MINGW_DIR%\bin"
        goto :EOF
    )
    if exist "%MINGW_DIR%\g++.exe" (
        set "MINGW_BIN=%MINGW_DIR%"
        goto :EOF
    )
)

REM 2. Check common Qt Tools paths (newest first)
for %%v in (1310_64 1220_64 1120_64 810_64) do (
    if exist "C:\Qt\Tools\mingw%%v\bin" (
        set "MINGW_BIN=C:\Qt\Tools\mingw%%v\bin"
        goto :EOF
    )
)

REM 3. Check MSYS2 default path
if exist "C:\msys64\mingw64\bin" (
    set "MINGW_BIN=C:\msys64\mingw64\bin"
    goto :EOF
)

REM 4. Fallback: search PATH
for %%X in (g++.exe) do (
    set "GXX_PATH=%%~$PATH:X"
    if not "%GXX_PATH%"=="" (
        for %%A in ("%GXX_PATH%\.") do set "MINGW_BIN=%%~fA"
        goto :EOF
    )
)
goto :EOF


:FindQtPath
REM Locates the Qt6 MinGW installation prefix (the directory containing bin/).
REM Search order: QT_DIR env var, common C:\Qt paths, qmake in PATH.
REM Sets the QT_PATH environment variable.
set "QT_PATH="

REM 1. Check environment override
if defined QT_DIR (
    if exist "%QT_DIR%\bin\qmake.exe" (
        set "QT_PATH=%QT_DIR%"
        goto :EOF
    )
)

REM 2. Check common Qt installation paths (newest version first)
for %%v in (6.10.1 6.10.0 6.9.2 6.9.1 6.9.0 6.8.2 6.8.1 6.8.0 6.7.2 6.7.1 6.7.0 6.6.3 6.6.2 6.6.1 6.5.3) do (
    if exist "C:\Qt\%%v\mingw_64" (
        set "QT_PATH=C:\Qt\%%v\mingw_64"
        goto :EOF
    )
)

REM 3. Fallback: derive from qmake location in PATH
for %%X in (qmake.exe) do (
    set "QMAKE_PATH=%%~$PATH:X"
    if not "!QMAKE_PATH!"=="" (
        for %%A in ("!QMAKE_PATH!\..\..") do set "QT_PATH=%%~fA"
        goto :EOF
    )
)
goto :EOF

REM =============================================================================
REM Validation Functions
REM =============================================================================

:VerifyFile
REM Usage: call :VerifyFile "path" "description"
REM Returns errorlevel 0 if file exists, 1 if missing.
set "FILE_PATH=%~1"
set "DESCRIPTION=%~2"
if not exist "!FILE_PATH!" (
    echo [ERROR] !DESCRIPTION! not found: !FILE_PATH!
    exit /b 1
)
exit /b 0

:VerifyDir
REM Usage: call :VerifyDir "path" "description"
REM Returns errorlevel 0 if directory exists, 1 if missing.
set "DIR_PATH=%~1"
set "DESCRIPTION=%~2"
if not exist "!DIR_PATH!" (
    echo [ERROR] !DESCRIPTION! not found: !DIR_PATH!
    exit /b 1
)
exit /b 0

:VerifyCommand
REM Usage: call :VerifyCommand "command" "description"
REM Returns errorlevel 0 if command is found in PATH, 1 otherwise.
set "COMMAND=%~1"
set "DESCRIPTION=%~2"
where /q "!COMMAND!"
if %errorlevel% neq 0 (
    echo [ERROR] !DESCRIPTION! not found in PATH
    exit /b 1
)
exit /b 0

REM =============================================================================
REM Path Utilities
REM =============================================================================

:NormalizePath
REM Usage: call :NormalizePath "path_variable_name"
REM Resolves the variable to its fully-qualified canonical form.
for %%A in ("%~1") do set "%~1=%%~fA"
exit /b 0

REM =============================================================================
REM Directory Operations
REM =============================================================================

:SafeRmDir
REM Usage: call :SafeRmDir "path"
REM Removes the directory and all contents if it exists.
if exist "%~1" (
    rmdir /s /q "%~1"
)
exit /b 0

:EnsureDir
REM Usage: call :EnsureDir "path"
REM Creates the directory if it does not already exist.
if not exist "%~1" mkdir "%~1"
exit /b 0

REM =============================================================================
REM Logging Functions
REM =============================================================================

:LogInfo
echo [INFO] %~1
exit /b 0

:LogWarning
echo [WARNING] %~1
exit /b 0

:LogError
echo [ERROR] %~1
exit /b 1

:LogStep
echo.
echo [STEP %1] %~2
exit /b 0

REM =============================================================================
REM File Operations
REM =============================================================================

:CopyWithCheck
REM Usage: call :CopyWithCheck "source" "destination" "description"
REM Copies a file and reports success or failure.
set "SRC=%~1"
set "DST=%~2"
set "DESC=%~3"
copy "!SRC!" "!DST!" >nul 2>&1
if exist "!DST!" (
    echo   - !DESC!: OK
    exit /b 0
) else (
    echo   [ERROR] !DESC!: FAILED
    exit /b 1
)

REM =============================================================================
REM Python Environment Management
REM =============================================================================

:SetupPythonVenv
REM Usage: call :SetupPythonVenv "dest_dir"
REM Creates a Python virtual environment if one does not already exist.
set "VENV_DIR=%~1"
set "PYTHON_CMD=python"

if not exist "!VENV_DIR!" (
    echo [INFO] Creating Python virtual environment...
    "!PYTHON_CMD!" -m venv "!VENV_DIR!"
    if not exist "!VENV_DIR!" (
        echo [ERROR] Failed to create Python virtual environment
        exit /b 1
    )
)

echo [INFO] Upgrading pip...
"!VENV_DIR!\Scripts\python.exe" -m pip install --upgrade pip --quiet >nul 2>&1
exit /b 0

:InstallPythonPackage
REM Usage: call :InstallPythonPackage "venv_dir" "package_name"
REM Installs a Python package into the specified virtual environment.
set "VENV_DIR=%~1"
set "PACKAGE=%~2"
"!VENV_DIR!\Scripts\python.exe" -m pip install "!PACKAGE!" --quiet >nul 2>&1
if !errorlevel! neq 0 (
    echo   [ERROR] Failed to install !PACKAGE!
    exit /b 1
)
echo   - !PACKAGE!: OK
exit /b 0

REM =============================================================================
REM CMake Configuration Helpers
REM =============================================================================

:ConfigureCMake
REM Usage: call :ConfigureCMake "build_dir" "generator" [additional cmake args...]
REM Runs CMake configuration unless a cache already exists.
REM Expects CMAKE_CMD and PROJECT_ROOT to be set in the calling environment.
set "BUILD_DIR=%~1"
set "GENERATOR=%~2"
shift
shift
set "CMAKE_ARGS=%*"

if not exist "!BUILD_DIR!" mkdir "!BUILD_DIR!"

if exist "!BUILD_DIR!\CMakeCache.txt" (
    echo [INFO] CMakeCache.txt found. Skipping configuration.
    exit /b 0
)

echo [INFO] Configuring CMake with !GENERATOR!...
"!CMAKE_CMD!" -S "!PROJECT_ROOT!" -B "!BUILD_DIR!" -G "!GENERATOR!" !CMAKE_ARGS!
if !errorlevel! neq 0 (
    echo [ERROR] CMake configuration failed
    exit /b 1
)
exit /b 0

:CleanCMakeCache
REM Usage: call :CleanCMakeCache "build_dir"
REM Removes the CMake cache file and generated files to force reconfiguration.
set "BUILD_DIR=%~1"
if exist "!BUILD_DIR!\CMakeCache.txt" del /q "!BUILD_DIR!\CMakeCache.txt"
if exist "!BUILD_DIR!\CMakeFiles"     rmdir /s /q "!BUILD_DIR!\CMakeFiles"
echo [INFO] Cleaned CMake cache
exit /b 0