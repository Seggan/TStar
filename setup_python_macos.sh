#!/usr/bin/env bash
# =============================================================================
# setup_python_macos.sh - macOS Python Virtual Environment Setup
# =============================================================================
#
# Creates a portable Python virtual environment under deps/python_venv/
# with all runtime dependencies needed by TStar's AI bridge scripts.
#
# The venv is created with --copies so that it remains functional when
# embedded inside the macOS app bundle (symlinks would break).
#
# Supported Python versions: 3.11, 3.12 (3.13+ avoided for wheel stability).
#
# =============================================================================

set -e

echo ">>> TStar Python Environment Setup (macOS) <<<"

# ---------------------------------------------------------------------------
# Resolve project root relative to this script.
# ---------------------------------------------------------------------------

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"
PROJECT_ROOT="$(pwd)"

DEPS_DIR="$PROJECT_ROOT/deps"
PYTHON_VENV="$DEPS_DIR/python_venv"

echo "Target directory: $PYTHON_VENV"

# =============================================================================
# Step 1 - Locate a compatible Python interpreter.
# =============================================================================
# The search order is:
#   1. Versioned commands in PATH (python3.11, python3.12)
#   2. Homebrew-installed interpreters (ARM and Intel prefixes)
#   3. Generic python3 if its version falls within the supported range
# =============================================================================

echo ""
echo "[STEP 1] Finding Python..."

COMPAT_VERSIONS=("3.11" "3.12")
PYTHON_CMD=""

# 1. Check for versioned commands in PATH.
for ver in "${COMPAT_VERSIONS[@]}"; do
    if command -v "python$ver" &> /dev/null; then
        PYTHON_CMD="python$ver"
        break
    fi
done

# 2. Probe Homebrew installation directories directly.
if [ -z "$PYTHON_CMD" ]; then
    for BASE in "/opt/homebrew" "/usr/local"; do
        [ -d "$BASE" ] || continue
        for ver in "${COMPAT_VERSIONS[@]}"; do
            BREW_PYTHON="$BASE/opt/python@$ver/bin/python$ver"
            if [ -x "$BREW_PYTHON" ]; then
                PYTHON_CMD="$BREW_PYTHON"
                break 2
            fi
        done
    done
fi

# 3. Fall back to generic python3 if within the compatible range.
if [ -z "$PYTHON_CMD" ] && command -v python3 &> /dev/null; then
    P3_VER=$(python3 -c 'import sys; print(f"{sys.version_info.major}.{sys.version_info.minor}")')
    P3_MAJOR=$(echo "$P3_VER" | cut -d. -f1)
    P3_MINOR=$(echo "$P3_VER" | cut -d. -f2)

    if [ "$P3_MAJOR" -eq 3 ] && [ "$P3_MINOR" -ge 11 ] && [ "$P3_MINOR" -le 12 ]; then
        PYTHON_CMD="python3"
    fi
fi

if [ -z "$PYTHON_CMD" ]; then
    echo "[ERROR] Compatible Python 3 (3.11 or 3.12) not found."
    echo "Install with: brew install python@3.11"
    exit 1
fi

PYTHON_DISPLAY_VERSION=$("$PYTHON_CMD" --version)
PYTHON_MAJOR_MINOR=$("$PYTHON_CMD" -c 'import sys; print(f"{sys.version_info.major}.{sys.version_info.minor}")')

echo "  Using: $PYTHON_CMD ($PYTHON_DISPLAY_VERSION)"

# =============================================================================
# Step 2 - Prepare the output directory.
# =============================================================================

echo ""
echo "[STEP 2] Preparing directory..."

mkdir -p "$DEPS_DIR"

if [ -d "$PYTHON_VENV" ]; then
    echo "  Removing existing virtual environment..."
    rm -rf "$PYTHON_VENV"
fi

# =============================================================================
# Step 3 - Create the virtual environment.
# =============================================================================
# --copies ensures the Python binary is copied rather than symlinked,
# which is required for portability inside the macOS app bundle.
# =============================================================================

echo ""
echo "[STEP 3] Creating virtual environment..."

"$PYTHON_CMD" -m venv --copies "$PYTHON_VENV"

if [ ! -f "$PYTHON_VENV/bin/python3" ]; then
    echo "[ERROR] Failed to create virtual environment."
    exit 1
fi

echo "  Virtual environment created."

# =============================================================================
# Step 4 - Upgrade pip and setuptools.
# =============================================================================

echo ""
echo "[STEP 4] Upgrading pip..."

"$PYTHON_VENV/bin/python3" -m pip install --upgrade pip setuptools wheel --quiet

# =============================================================================
# Step 5 - Install runtime dependencies.
# =============================================================================
# NumPy version selection:
#   - Python 3.12+ requires NumPy >= 1.26.0.
#   - Python 3.11  can use the lighter 1.24.x/1.25.x builds.
# =============================================================================

echo ""
echo "[STEP 5] Installing dependencies..."

if [ "$(echo "$PYTHON_MAJOR_MINOR >= 3.12" | bc -l 2>/dev/null || python3 -c "print($PYTHON_MAJOR_MINOR >= 3.12)")" == "True" ] || [ "$PYTHON_MAJOR_MINOR" == "3.12" ]; then
    NUMPY_VERSION="numpy>=1.26.0,<2.0.0"
else
    NUMPY_VERSION="numpy>=1.24.0,<1.26.0"
fi

PACKAGES=(
    "$NUMPY_VERSION"
    "scipy<1.13.0"
    "tifffile"
    "imagecodecs"
    "astropy"
    "onnxruntime<1.18.0"
)

echo "  Installing packages..."
"$PYTHON_VENV/bin/python3" -m pip install "${PACKAGES[@]}" --quiet

# =============================================================================
# Step 6 - Verify that all packages are importable.
# =============================================================================

echo ""
echo "[STEP 6] Verifying installation..."

for pkg in "${PACKAGES[@]}"; do
    # Derive the Python import name from the pip specifier.
    pkg_name=$(echo "$pkg" | sed 's/[<>=!].*//' | tr '-' '_')
    if "$PYTHON_VENV/bin/python3" -c "import $pkg_name" 2>/dev/null; then
        echo "  $pkg: OK"
    else
        echo "  $pkg: FAILED"
    fi
done

# =============================================================================
# Done
# =============================================================================

echo ""
echo ">>> Python environment setup complete. <<<"
echo ""
echo "Python executable: $PYTHON_VENV/bin/python3"
echo ""
echo "Next step:"
echo "  ./src/build_macos.sh"