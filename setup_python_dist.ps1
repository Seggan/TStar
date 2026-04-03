# =============================================================================
# setup_python_dist.ps1 - Windows Embeddable Python Environment Setup
# =============================================================================
#
# Downloads the Python embeddable distribution, installs pip, and installs
# all runtime dependencies required by TStar's AI bridge scripts.
#
# The resulting environment is self-contained in deps/python/ and is later
# bundled into the distribution package by package_dist.bat.
#
# =============================================================================

$ErrorActionPreference = "Stop"

# ---------------------------------------------------------------------------
# Configuration constants.
# ---------------------------------------------------------------------------

$PYTHON_VER  = "3.11.9"
$PYTHON_URL  = "https://www.python.org/ftp/python/$PYTHON_VER/python-$PYTHON_VER-embed-amd64.zip"
$PIP_URL     = "https://bootstrap.pypa.io/get-pip.py"

$DEPS_DIR    = Join-Path $PSScriptRoot "deps"
$PYTHON_DIR  = Join-Path $DEPS_DIR "python"
$PthFile     = Join-Path $PYTHON_DIR "python311._pth"

Write-Host ">>> TStar Python Environment Setup <<<"
Write-Host "Target directory: $PYTHON_DIR"

# ---------------------------------------------------------------------------
# Step 1 - Prepare the output directory.
# ---------------------------------------------------------------------------

if (!(Test-Path $DEPS_DIR)) {
    New-Item -ItemType Directory -Path $DEPS_DIR | Out-Null
}
if (Test-Path $PYTHON_DIR) {
    Write-Host "Removing existing Python directory..."
    Remove-Item -Path $PYTHON_DIR -Recurse -Force
}
New-Item -ItemType Directory -Path $PYTHON_DIR | Out-Null

# ---------------------------------------------------------------------------
# Step 2 - Download the Python embeddable package (cached).
# ---------------------------------------------------------------------------

$ZipPath = Join-Path $DEPS_DIR "python.zip"
if (!(Test-Path $ZipPath)) {
    Write-Host "Downloading Python $PYTHON_VER embeddable package..."
    Invoke-WebRequest -Uri $PYTHON_URL -OutFile $ZipPath
} else {
    Write-Host "Using cached Python archive."
}

# ---------------------------------------------------------------------------
# Step 3 - Extract the archive.
# ---------------------------------------------------------------------------

Write-Host "Extracting Python..."
Expand-Archive -Path $ZipPath -DestinationPath $PYTHON_DIR -Force

# ---------------------------------------------------------------------------
# Step 4 - Enable site-packages in the ._pth file.
# The embeddable distribution ships with 'import site' commented out.
# Pip and third-party packages require it to be enabled.
# ---------------------------------------------------------------------------

Write-Host "Configuring python311._pth to enable site-packages..."
$content = Get-Content $PthFile
$content = $content -replace "#import site", "import site"
$content | Set-Content $PthFile

# ---------------------------------------------------------------------------
# Step 5 - Bootstrap pip.
# ---------------------------------------------------------------------------

Write-Host "Installing pip..."
$GetPip    = Join-Path $PYTHON_DIR "get-pip.py"
$PythonExe = Join-Path $PYTHON_DIR "python.exe"

Invoke-WebRequest -Uri $PIP_URL -OutFile $GetPip
& $PythonExe $GetPip --no-warn-script-location

# ---------------------------------------------------------------------------
# Step 6 - Install runtime dependencies.
#
# onnxruntime-directml is used instead of plain onnxruntime so that
# AMD, Intel, and NVIDIA GPUs are all supported via DirectML on Windows.
# ---------------------------------------------------------------------------

Write-Host "Installing Python dependencies..."
& $PythonExe -m pip install `
    "numpy>=1.24.0,<1.26.0" `
    "scipy<1.13.0" `
    tifffile `
    imagecodecs `
    astropy `
    "onnxruntime-directml<1.18.0" `
    --no-warn-script-location

# ---------------------------------------------------------------------------
# Cleanup - remove the pip bootstrap script.
# ---------------------------------------------------------------------------

Remove-Item $GetPip -Force

Write-Host ">>> Python environment setup complete. <<<"
Write-Host "You can now run package_dist.bat to create a distribution."