#!/bin/bash
# =============================================================================
# TStar Build Script for Linux
# Equivalent of build_macos.sh but adapted for Linux environments.
# =============================================================================

safe_rm_rf() {
    local path="$1"
    if [ -d "$path" ] || [ -L "$path" ]; then
        rm -rf "$path"
    fi
}

ensure_dir() {
    local dir="$1"
    if [ ! -d "$dir" ]; then
        mkdir -p "$dir"
    fi
}

set -e  # Exit on error

# Check for --clean flag
CLEAN_MODE=0
if [ "$1" == "--clean" ]; then
    CLEAN_MODE=1
fi

# Check for --lto-on flag
LTO_MODE="OFF"
for arg in "$@"; do
    if [ "$arg" == "--lto-on" ]; then
        LTO_MODE="ON"
    fi
done

echo "==========================================="
echo " TStar Build Script (Linux)"
if [ $CLEAN_MODE -eq 1 ]; then
    echo " (CLEAN MODE - Reconfiguring CMake)"
fi
echo "==========================================="

# Move to project root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR/.."
PROJECT_ROOT="$(pwd)"

# --- CONFIGURATION ---
CMAKE_CMD="cmake"
BUILD_TYPE="Release"
BUILD_DIR="build"
GENERATOR="Unix Makefiles"

# Check for Ninja (faster builds)
if command -v ninja &> /dev/null; then
    GENERATOR="Ninja"
    echo "[INFO] Using Ninja generator"
fi

# --- 1. INSTALL PREREQUISITES ---
echo ""
echo "[STEP 1] Installing prerequisites..."

# Check other dependencies
DEPS=(build-essential libgl1-mesa-dev qt6-base-dev qt6-svg-dev qt6-tools-dev cmake clazy g++ ninja-build pkg-config libopencv-dev libgsl-dev libcfitsio-dev libomp-dev libmd4c-dev liblcms2-dev)
for dep in "${DEPS[@]}"; do
    if ! dpkg -l "$dep" > /dev/null; then
        echo "[WARNING] $dep not found. Install with: sudo apt install $dep"
        NOT_FOUND_DEPS=1
    else
        echo "  - $dep: OK"
    fi
done

if [ "$NOT_FOUND_DEPS" -eq 1 ]; then
    echo ""
    echo "[ERROR] Some dependencies are missing. Please install them and re-run the script."
    exit 1
fi

# Optional deps
for dep in liblz4-dev libzstd-dev libraw-dev; do
    if ! dpkg -l "$dep" > /dev/null; then
        echo "  - $dep: NOT FOUND (optional)"
    else
        echo "  - $dep: OK"
    fi
done

# --- 2. SETUP PYTHON ENVIRONMENT ---
echo ""
echo "[STEP 2] Checking Python environment..."

PYTHON_VENV="$PROJECT_ROOT/deps/python_venv"
if [ ! -f "$PYTHON_VENV/bin/python3" ]; then
    echo "[INFO] Python venv not found. Running setup script..."
    if [ -f "$PROJECT_ROOT/setup_python_linux.sh" ]; then
        chmod +x "$PROJECT_ROOT/setup_python_linux.sh"
        "$PROJECT_ROOT/setup_python_linux.sh"
    else
        echo "[WARNING] setup_python_linux.sh not found. Python bridge may not work."
    fi
else
    echo "  - Python venv: OK"
fi

# --- 3. CMAKE CONFIGURATION ---
echo "[STEP 3] Configuring CMake..."

ensure_dir "$BUILD_DIR"

# Clean CMake cache if requested
if [ $CLEAN_MODE -eq 1 ]; then
    echo "[INFO] Cleaning CMake cache..."
    safe_rm_rf "$BUILD_DIR/CMakeCache.txt"
    safe_rm_rf "$BUILD_DIR/CMakeFiles"
fi

if [ -f "$BUILD_DIR/CMakeCache.txt" ] && [ $CLEAN_MODE -eq 0 ]; then
    echo "[INFO] CMakeCache.txt found. Skipping configuration."
else
    "$CMAKE_CMD" -S . -B "$BUILD_DIR" \
        -G "$GENERATOR" \
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
        -DCMAKE_PREFIX_PATH="$QT_PREFIX" \
        -DCMAKE_OSX_DEPLOYMENT_TARGET="11.0" \
        -DENABLE_LTO="$LTO_MODE"
    
    if [ $? -ne 0 ]; then
        echo "[ERROR] CMake configuration failed!"
        exit 1
    fi
fi

# --- 4. BUILD ---
echo ""
echo "[STEP 4] Building TStar..."

NUM_CORES=$(nproc --all)
"$CMAKE_CMD" --build "$BUILD_DIR" --config "$BUILD_TYPE" --parallel "$NUM_CORES"

if [ $? -ne 0 ]; then
    echo "[ERROR] Build failed!"
    exit 1
fi

# --- 5. VERIFY BUILD ---
echo ""
echo "[STEP 5] Verifying build..."

if [ -f "$BUILD_DIR/TStar" ]; then
    echo "  - TStar (binary): OK"
    EXECUTABLE="$BUILD_DIR/TStar"
else
    echo "[ERROR] Build output not found!"
    exit 1
fi

echo ""
echo "==========================================="
echo " SUCCESS!"
echo " Executable: $EXECUTABLE"
echo "==========================================="
echo ""
echo "Next steps:"
echo "  1. Run: $EXECUTABLE"
