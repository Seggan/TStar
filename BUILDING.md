# Building TStar from Source

This document provides complete instructions for building TStar on all supported platforms.
Read the section that applies to your operating system.

---

## Table of Contents

- [Windows (MinGW)](#building-on-windows)
  - [Prerequisites](#prerequisites-windows)
  - [Qt Installation](#qt-installation)
  - [Dependency Setup](#dependency-setup-windows)
  - [Python Environment](#python-environment-windows)
  - [Build Steps](#build-steps-windows)
  - [Deployment and Distribution](#deployment--distribution-windows)
  - [Troubleshooting](#troubleshooting-windows)
- [macOS](#building-on-macos)
  - [Prerequisites](#prerequisites-macos)
  - [Dependency Setup](#dependency-setup-macos)
  - [Python Environment](#python-environment-macos)
  - [Build Steps](#build-steps-macos)
  - [Output](#output-macos)
  - [Troubleshooting](#troubleshooting-macos)
- [CMake Options](#cmake-options)
- [Verified Configurations](#verified-configurations)

---

## Building on Windows

### Prerequisites (Windows)

The following tools are required to build TStar on Windows. All version constraints have been
verified against the configurations listed in the [Verified Configurations](#verified-configurations)
table at the end of this document.

| Software | Minimum Version | Notes |
|---|---|---|
| Qt6 | 6.5 | Download via [Qt Online Installer](https://www.qt.io/download-qt-installer). Select the MinGW 64-bit component. |
| CMake | 3.16 | Bundled with Qt installation, or download from [cmake.org](https://cmake.org/download/). |
| MinGW-w64 | GCC 11 | Bundled with Qt installation. Select MinGW 13.x.x 64-bit from Developer Tools. |
| Git | Any recent | [git-scm.com](https://git-scm.com/download/win) |
| Ninja | Any recent | Recommended generator. Bundled with Qt installation. |

### Qt Installation

1. Download and run the [Qt Online Installer](https://www.qt.io/download-qt-installer).
2. Under the Qt version selector, expand **Qt 6.x.x** and select **MinGW 64-bit**.
3. Under **Developer and Designer Tools**, select:
   - CMake (if not already installed system-wide)
   - MinGW 13.x.x 64-bit (or the latest available version)
4. Make note of the installation path (for example, `C:\Qt\6.7.0\mingw_64`). This path is
   passed to CMake as `CMAKE_PREFIX_PATH`.

---

### Dependency Setup (Windows)

TStar requires several external libraries. Place them in a `deps/` directory at the project root.
The CMake build system resolves all paths from that location automatically.

**Required directory layout:**

```
TStar/
|-- deps/
|   |-- cfitsio/
|   |   |-- include/
|   |   |   `-- fitsio.h
|   |   `-- lib/
|   |       `-- libcfitsio.a
|   |-- opencv/
|   |   |-- include/
|   |   `-- lib/
|   |-- gsl/
|   |   |-- include/
|   |   `-- lib/
|   |-- lcms2/              (vendored, already in the repository)
|   |   |-- include/
|   |   `-- src/
|   |-- libraw/             (optional - RAW image support)
|   |   |-- libraw/
|   |   |   `-- libraw.h
|   |   |-- lib/
|   |   `-- bin/
|   |-- lz4/               (optional - XISF LZ4 compression)
|   |   |-- include/
|   |   `-- static/
|   `-- zstd/              (optional - XISF Zstd compression)
|       |-- include/
|       `-- static/
|-- src/
|-- CMakeLists.txt
`-- ...
```

#### CFITSIO

**Option A: Pre-built binaries (recommended)**

Download pre-built MinGW binaries from the [CFITSIO releases page](https://heasarc.gsfc.nasa.gov/fitsio/)
and extract the archive contents into `deps/cfitsio/`.

**Option B: Build from source**

Open an MSYS2/MinGW terminal and run:

```bash
wget https://heasarc.gsfc.nasa.gov/FTP/software/fitsio/c/cfitsio-4.3.0.tar.gz
tar -xzf cfitsio-4.3.0.tar.gz
cd cfitsio-4.3.0
./configure --prefix=/path/to/TStar/deps/cfitsio
make && make install
```

#### OpenCV

1. Download the Windows package from [opencv.org/releases](https://opencv.org/releases/).
2. Extract the archive and copy the `include/` directory and the MinGW-compatible libraries from `lib/`
   into `deps/opencv/`.

#### GSL (GNU Scientific Library)

The simplest approach on Windows is to install GSL via MSYS2:

```bash
pacman -S mingw-w64-x86_64-gsl
```

Then copy the `include/` and `lib/` directories from your MSYS2 installation into `deps/gsl/`.

Alternatively, follow the [GSL build documentation](https://www.gnu.org/software/gsl/).

#### LibRaw (Optional -- RAW Image Support)

LibRaw enables native loading of camera RAW files (CR2, NEF, ARW, DNG, and many others).

1. Download pre-built Windows binaries from the [LibRaw download page](https://www.libraw.org/download).
2. Extract and place the contents into `deps/libraw/`.
3. Ensure `libraw.dll` is present in `deps/libraw/bin/`.

When found, CMake automatically defines the `HAVE_LIBRAW` preprocessor flag.

#### LZ4 (Optional -- XISF Compression)

Enables fast LZ4 compression when reading and writing XISF files.

1. Download a release from [github.com/lz4/lz4/releases](https://github.com/lz4/lz4/releases).
2. Extract into `deps/lz4/`. The static library must be at `deps/lz4/static/liblz4_static.lib`.

When found, CMake automatically defines the `HAVE_LZ4` preprocessor flag.

#### Zstd (Optional -- XISF Compression)

Enables high-ratio Zstandard compression when reading and writing XISF files.

1. Download a release from [github.com/facebook/zstd/releases](https://github.com/facebook/zstd/releases).
2. Extract into `deps/zstd/`. The static library must be at `deps/zstd/static/libzstd_static.lib`.

When found, CMake automatically defines the `HAVE_ZSTD` preprocessor flag.

---

### Python Environment (Windows)

TStar bundles a Python embeddable distribution for its AI tools and bridge scripts. The setup script
downloads, extracts, and configures the environment automatically.

Run the following from the project root in a PowerShell terminal:

```powershell
.\setup_python_dist.ps1
```

This script performs the following steps:
1. Downloads the Python 3.11 embeddable zip package.
2. Extracts it into `deps/python/`.
3. Patches the `._pth` file to enable `site-packages`.
4. Installs `pip` using the official `get-pip.py` bootstrap.
5. Installs all required runtime packages:
   - `numpy`, `scipy`, `tifffile`, `imagecodecs`, `astropy`, `onnxruntime-directml`

> **Note on GPU support:** `onnxruntime-directml` is used instead of plain `onnxruntime` so that
> AMD, Intel, and NVIDIA GPUs are all supported on Windows via the DirectML backend.

#### Python Discovery at Runtime

The C++ application searches for the Python executable in the following order at startup:

| Priority | Path | Context |
|---|---|---|
| 1 | `./python/python.exe` | Distribution / production build |
| 2 | `../deps/python/python.exe` | Development build environment |
| 3 | System `python3` (via PATH) | Fallback |
| 4 | Literal `python3` | Last resort invocation |

---

### Build Steps (Windows)

1. Open a terminal with the Qt environment active. If you installed Qt via the installer, use the
   **Qt 6.x.x MinGW 64-bit** shortcut from the Start Menu.

2. Clone the repository (if you have not already):

   ```bash
   git clone <repository-url>
   cd TStar
   ```

3. Set up the Python environment (required for AI tools):

   ```powershell
   .\setup_python_dist.ps1
   ```

4. **Option A: All-in-one build script (recommended)**

   ```bash
   src/build_all.bat
   ```

5. **Option B: Manual CMake build (Ninja generator recommended)**

   ```bash
   mkdir build && cd build
   cmake -G "Ninja" ^
       -DCMAKE_PREFIX_PATH="C:/Qt/6.7.0/mingw_64" ^
       -DCMAKE_BUILD_TYPE=Release ^
       ..
   cmake --build . --config Release -j4
   ```

   Replace `C:/Qt/6.7.0/mingw_64` with the actual path to your Qt installation.

---

### Deployment & Distribution (Windows)

To produce a standalone, portable distribution folder:

```bash
src/package_dist.bat
```

This script automates the following steps:

1. Verifies that `TStar.exe` has been built successfully.
2. Runs `setup_python_dist.ps1` if the Python environment is not already present.
3. Copies all Qt DLLs and plugins using `windeployqt`.
4. Copies MinGW, GSL, and OpenCV runtime libraries.
5. Copies optional dependency libraries (LibRaw DLL, LZ4, Zstd) if they are present.
6. Bundles the Python environment into `dist/TStar/python/`.
7. Copies all scripts from `src/scripts/` into `dist/TStar/scripts/`.
8. Generates an end-user `README.txt` in the distribution root.

The resulting `dist/TStar/` directory is completely self-contained and can be archived or moved
to any Windows x64 machine without requiring any pre-installed dependencies.

> **Note:** The legacy `deploy.bat` script is preserved for backward compatibility but is no longer
> the recommended method. Use `package_dist.bat` instead.

---

### Troubleshooting (Windows)

**AI tools fail to start or report Python errors:**
- Verify that `deps/python/python.exe` exists. If not, run `setup_python_dist.ps1` again.
- To reset the environment, delete the `deps/python/` directory and re-run the setup script.

**Missing DLLs at runtime:**
- If you built manually with CMake without using `package_dist.bat`, the runtime DLLs will not be
  present next to the executable. Always run `src/package_dist.bat` before distributing or testing
  outside the Qt terminal environment.

**CMake cannot find Qt:**
- Ensure `CMAKE_PREFIX_PATH` points to the correct Qt MinGW installation directory, for example
  `C:/Qt/6.7.0/mingw_64`.

**AutoMoc failures on Windows/MinGW:**
- The build system disables `CMAKE_AUTOMOC_COMPILER_PREDEFS` for Windows to work around a known
  MinGW pre-processing issue. This is handled automatically; no manual intervention is required.

---

## Building on macOS

### Prerequisites (macOS)

| Software | Installation |
|---|---|
| Xcode Command Line Tools | `xcode-select --install` |
| Homebrew | See [brew.sh](https://brew.sh) |
| Qt6 | `brew install qt@6` |
| CMake | `brew install cmake` |
| Ninja (recommended) | `brew install ninja` |

**Architecture notes:**
- **Apple Silicon (M1/M2/M3/M4):** Homebrew installs to `/opt/homebrew`. The build system detects
  this automatically.
- **Intel Macs:** Homebrew installs to `/usr/local`. The build system detects this automatically.
- Universal binaries are not currently produced; the build targets the native architecture of the
  build machine.

---

### Dependency Setup (macOS)

All required and optional dependencies are available through Homebrew:

```bash
# Required dependencies
brew install qt@6 cmake ninja pkg-config
brew install opencv gsl cfitsio libomp md4c

# Optional: XISF compression support (recommended)
brew install lz4 zstd

# Optional: Camera RAW support (CR2, NEF, ARW, DNG, etc.)
brew install libraw

# Python for AI tools
brew install python@3.11
```

CMake automatically detects all Homebrew-installed libraries by querying `brew --prefix <package>`
at configure time. No manual path configuration is required.

---

### Python Environment (macOS)

Run the setup script once before building to create the Python virtual environment:

```bash
chmod +x setup_python_macos.sh
./setup_python_macos.sh
```

This script:
1. Searches for a compatible Python interpreter (3.11 or 3.12) in PATH, Homebrew, and standard locations.
2. Creates a virtual environment at `deps/python_venv/` using `--copies` so it is portable when
   embedded in the app bundle (symlinked executables break when copied).
3. Upgrades pip, setuptools, and wheel.
4. Installs all required packages: `numpy`, `scipy`, `tifffile`, `imagecodecs`, `astropy`, `onnxruntime`.
5. Verifies each package is importable.

> **Why Python 3.11/3.12?** Python 3.13 and later are avoided because binary wheel availability
> for `numpy < 2.0` and `onnxruntime < 1.18` is limited on the newest interpreter versions,
> which would require building from source.

#### Python Discovery at Runtime (macOS)

| Priority | Path | Context |
|---|---|---|
| 1 | `$APP_DIR/../Resources/python_venv/bin/python3` | Distribution app bundle |
| 2 | `$APP_DIR/../../deps/python_venv/bin/python3` | Development build |
| 3 | System `python3` (via `QStandardPaths::findExecutable()`) | Fallback |
| 4 | Literal `python3` | Last resort invocation |

---

### Build Steps (macOS)

```bash
# Step 1: Set up the Python environment (one-time setup)
chmod +x setup_python_macos.sh
./setup_python_macos.sh

# Step 2: Build the application
chmod +x src/build_macos.sh
./src/build_macos.sh

# Step 3: Create a distribution-ready app bundle
chmod +x src/package_macos.sh
./src/package_macos.sh

# Step 4 (optional): Create a DMG installer
# Install create-dmg for styled installers: brew install create-dmg
chmod +x src/build_installer_macos.sh
./src/build_installer_macos.sh
```

> **Development note:** Step 6 of `build_macos.sh` creates a symlink from `src/scripts/` into the
> app bundle's `Resources/scripts/` directory. This allows you to edit and test scripts without
> rebuilding the application.

---

### Output (macOS)

| Artifact | Location |
|---|---|
| App Bundle (development) | `build/TStar.app` |
| App Bundle (distribution) | `dist/TStar.app` |
| DMG Installer | `installer_output/TStar_Setup_X.X.X.dmg` |

---

### Troubleshooting (macOS)

**Gatekeeper blocks the application on first launch:**
- Right-click the app and select **Open** to bypass the unsigned app warning.
- For public distribution, consider notarizing with `xcrun notarytool`.

**AI tools fail to start or report Python errors:**
- Verify that `deps/python_venv/bin/python3` exists.
- To reset, delete `deps/python_venv/` and re-run `setup_python_macos.sh`.

**Missing dylib errors at launch:**
- Ensure you ran `src/package_macos.sh` after building. This script copies all Homebrew dylibs
  into `TStar.app/Contents/Frameworks/` and fixes the RPATH entries.

**Checking the build architecture:**
```bash
lipo -info build/TStar.app/Contents/MacOS/TStar
# or
file build/TStar.app/Contents/MacOS/TStar
```

**libomp not found:**
```bash
brew install libomp
```

**OpenCV configure error (dnn/OpenVINO):**
- This is expected. The CMake configuration explicitly excludes the `dnn` module to avoid the
  OpenVINO dependency. No action is required.

---

## CMake Options

| Option | Default | Description |
|---|---|---|
| `CMAKE_BUILD_TYPE` | `Debug` | Set to `Release` for an optimized build. |
| `CMAKE_PREFIX_PATH` | _(empty)_ | Path to the Qt installation root (required on Windows). |
| `ENABLE_LTO` | `OFF` | Enable Link-Time Optimization. Applies to Release builds only. Increases link time significantly but reduces binary size and may improve runtime performance. |

---
## Building on Debian Linux
### Install dependencies
```bash
sudo apt update
sudo apt install build-essential libgl1-mesa-dev
sudo apt install qt6-base-dev qt6-svg-dev qt6-tools-dev cmake clazy g++
sudo apt install ninja-build pkg-config
sudo apt install libopencv-dev libgsl-dev libcfitsio-dev libomp-dev libmd4c-dev liblcms2-dev libcurl4-gnutls-dev
```

Additionally, Python 3.11 or 3.12 is required for the AI tools and bridge scripts.

### Install optional dependencies for XISF compression and RAW support
```bash
sudo apt install liblz4-dev libzstd-dev
sudo apt install libraw-dev
```

### Build the application
```bash
# 1. Setup Python environment
chmod +x setup_python_linux.sh
./setup_python_linux.sh

# 2. Build the application
chmod +x src/build_linux.sh
./src/build_linux.sh

# 3. Create distribution package
chmod +x src/package_linux.sh
./src/package_linux.sh
```

A `.deb` package will be created in the `dist/` folder for easy installation on Debian-based systems.

---

## Verified Configurations

| Operating System | Compiler | Qt Version | Status |
|---|---|---|---|
| Windows 11 | MinGW 13.1 (GCC) | 6.7.x, 6.8.x, 6.10.x | Tested |
| Windows 10 | MinGW 11.2 (GCC) | 6.5.0 | Tested |
| macOS 11+ (Apple Silicon) | Apple Clang | 6.5 through 6.10 | Tested |
| macOS 11+ (Intel) | Apple Clang | 6.5 through 6.10 | Tested |