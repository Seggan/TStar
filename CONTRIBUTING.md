# Contributing to TStar

Thank you for your interest in contributing to TStar. This document describes the workflows,
standards, and conventions that all contributors are expected to follow. Reading it in full
before opening a pull request will make the review process faster for everyone.

---

## Table of Contents

- [How to Contribute](#how-to-contribute)
  - [Reporting Bugs](#reporting-bugs)
  - [Suggesting Features](#suggesting-features)
  - [Code Contributions](#code-contributions)
- [Code Style](#code-style)
  - [C++ Guidelines](#c-guidelines)
  - [Optional Feature Guards](#optional-feature-guards)
  - [Python Guidelines](#python-guidelines)
  - [Translations](#translations)
- [Platform-Specific Considerations](#platform-specific-considerations)
  - [macOS Development](#macos-development)
  - [Windows Development](#windows-development)
- [Commit Message Format](#commit-message-format)
- [Pull Request Process](#pull-request-process)
- [Development Tips](#development-tips)

---

## How to Contribute

### Reporting Bugs

Before opening a bug report, please check the [existing issues](../../issues) to avoid duplicates.

When filing a report, include the following information:

- **TStar version** (visible in the About dialog or in `changelog.txt`)
- **Operating system** and version (for example, Windows 11 22H2, macOS 14.4 Apple Silicon)
- **Steps to reproduce** the issue in minimal form
- **Expected behavior** versus **actual behavior**
- **Log output** from the log panel or the log file, if relevant
- **Sample image** if the issue is format or data specific (use the smallest file that
  reproduces the problem)

### Suggesting Features

1. Open an issue using the feature request template.
2. Describe the use case clearly: who benefits, in what workflow, and how often.
3. Describe the expected user interaction and any relevant UI changes.
4. Consider whether the feature fits the project scope (astrophotography image processing).
5. If you intend to implement it yourself, mention this in the issue so it can be discussed
   before you invest time writing code.

### Code Contributions

1. **Fork** the repository on GitHub.
2. **Create a feature branch** from `main`:

   ```bash
   git checkout -b feature/your-descriptive-feature-name
   ```

3. **Write code** following the style guidelines described below.
4. **Test** your changes with several image types (FITS 16-bit integer, FITS 32-bit float,
   TIFF, RAW) and on at least the platform you have access to.
5. **Commit** using the format described in the [Commit Message Format](#commit-message-format)
   section.
6. **Push** your branch and open a Pull Request against `main`.

---

## Code Style

### C++ Guidelines

TStar is written in C++17. All new code must conform to the following conventions.

**Language standard:** C++17. Avoid features from later standards.

**Naming conventions:**

| Entity | Convention | Example |
|---|---|---|
| Classes and structs | `PascalCase` | `ImageBuffer`, `StarDetector` |
| Functions and methods | `camelCase` | `processImage()`, `computeMedian()` |
| Member variables | `m_camelCase` | `m_threadCount`, `m_outputPath` |
| Local variables | `camelCase` | `pixelValue`, `rowCount` |
| Constants and enumerators | `UPPER_SNAKE_CASE` | `MAX_ITERATIONS`, `DEFAULT_SIGMA` |
| File names | `PascalCase` matching the primary class | `StarDetector.cpp`, `StarDetector.h` |

**Formatting:**

- 4 spaces per indentation level. No tabs.
- Opening braces on the same line as the statement for functions and control flow.
- One blank line between logically distinct blocks within a function.
- Keep lines under 100 characters where practical.
- Do not leave commented-out code in committed files unless accompanied by a comment
  explaining why it is preserved.

**Qt conventions:**

- Follow Qt naming conventions for signals (`signalName`) and slots (`onActionName`).
- Prefer `connect()` with pointer-to-member syntax over the string-based `SIGNAL()`/`SLOT()` macros.
- Use `Q_OBJECT` in every class that declares signals or slots.

**Header files:**

- Use `#pragma once` as the include guard.
- Keep headers minimal. Forward-declare types where full definitions are not required.
- Do not include implementation details in headers.

**Error handling:**

- Prefer returning error codes or using `std::optional` over throwing exceptions in
  performance-critical paths.
- Use Qt's `qWarning()` and `qCritical()` for runtime diagnostics rather than raw `fprintf`.

**Example:**

```cpp
class ImageProcessor
{
    Q_OBJECT

public:
    explicit ImageProcessor(QObject* parent = nullptr);

    bool processImage(const ImageBuffer& buffer);

signals:
    void progressUpdated(int percent);

private:
    int  m_threadCount;
    bool m_cancelRequested;

    static constexpr int MAX_ITERATIONS = 100;
};
```

---

### Optional Feature Guards

TStar supports several optional library dependencies that are enabled at build time when the
corresponding library is found by CMake. Code that depends on these libraries must always be
guarded with the appropriate preprocessor macro so that the project builds correctly when the
library is absent.

| Library | Preprocessor flag | Feature |
|---|---|---|
| LibRaw | `HAVE_LIBRAW` | Camera RAW file loading |
| LZ4 | `HAVE_LZ4` | LZ4 compression in XISF files |
| Zstd | `HAVE_ZSTD` | Zstd compression in XISF files |

**Required pattern:**

```cpp
#ifdef HAVE_LIBRAW
    // LibRaw-dependent code
#endif

#ifdef HAVE_LZ4
    // LZ4 compression code
#endif

#ifdef HAVE_ZSTD
    // Zstd compression code
#endif
```

Never assume a library is present. Always provide a graceful fallback or a clear error message
when an optional feature is requested but unavailable.

---

### Python Guidelines

Python is used exclusively for bridge and worker scripts that interface with external AI tools.
Python code is not part of the core processing pipeline.

- **Python version:** All scripts must be compatible with Python 3.11. Avoid syntax or standard
  library features introduced in 3.12 or later.
- **Dependencies:** Any new pip dependency must be added to both `setup_python_dist.ps1`
  (Windows) and `setup_python_macos.sh` (macOS) and explicitly discussed in the associated
  pull request. Do not add heavy or obscure packages without justification.
- **Script location:** All scripts must reside in `src/scripts/`. Do not place scripts elsewhere.
- **Style:** Follow [PEP 8](https://peps.python.org/pep-0008/) formatting conventions.
- **Python discovery:** C++ code resolves the Python executable via a prioritized fallback chain
  (bundled environment first, then system PATH). Write scripts such that they function correctly
  when invoked from a virtualenv or an embeddable Python distribution.
- **No global state:** Scripts should be idempotent where possible and must not modify files
  outside their designated input/output paths.

---

### Translations

UI strings in C++ source code are made translatable using Qt's `tr()` mechanism. Translation data
is managed via Python dictionaries in `tools/trans_data.py`. The `translate_manager.py` script
uses this dictionary to generate `.ts` files for Qt Linguist.

**When adding a new user-facing string:**

1. Wrap the string in `tr()` in the C++ source.
2. Add the corresponding translation entries for all supported languages to `tools/trans_data.py`.
3. Run `translate_manager.py` to regenerate the `.ts` files.
4. Do not edit `.ts` or `.qm` files by hand.

---

## Platform-Specific Considerations

### macOS Development

**Architecture detection:**
The CMake build system and all shell scripts automatically detect whether the host machine is
Apple Silicon (arm64) or Intel (x86_64) and use the appropriate Homebrew prefix accordingly:

- Apple Silicon: `/opt/homebrew`
- Intel: `/usr/local`

**Cross-architecture testing:**
If you have access to both architectures, test your changes on both before submitting a pull
request, particularly if your changes touch:
- Low-level image processing or SIMD code
- File I/O or endianness-sensitive paths
- Dependency detection in CMakeLists.txt or build scripts

**Verifying the build architecture:**
```bash
lipo -info build/TStar.app/Contents/MacOS/TStar
# or
file build/TStar.app/Contents/MacOS/TStar
```

**Shared build logic:**
Common detection logic and helper functions for macOS builds are centralized in `src/macos_utils.sh`.
Update this file when adding features that affect the build or packaging pipeline.

**Library bundling:**
The `src/package_macos.sh` script copies all required dylibs into `TStar.app/Contents/Frameworks/`
and patches RPATH entries accordingly. If you add a new optional dependency, update this script
to bundle it when present.

---

### Windows Development

**Shared build logic:**
Common helper functions and detection logic for Windows builds are centralized in `src/windows_utils.bat`.
Update this file when adding features that affect the build or packaging pipeline.

**AutoMoc on MinGW:**
The `CMAKE_AUTOMOC_COMPILER_PREDEFS` flag is disabled for Windows builds to work around a known
MinGW pre-processing issue. This is handled automatically and requires no manual intervention.

---

## Commit Message Format

Commit messages must follow the [Conventional Commits](https://www.conventionalcommits.org/) style,
adapted as follows:

**Format:**
```
<type>: <short summary in present tense, max 72 characters>

<optional body: explain what and why, wrapped at 72 characters>
```

**Common types:**

| Type | Use for |
|---|---|
| `feat` | A new feature or capability |
| `fix` | A bug fix |
| `refactor` | Code restructuring without behavior change |
| `perf` | A performance improvement |
| `docs` | Documentation changes only |
| `build` | Changes to the build system or dependencies |
| `test` | Adding or modifying tests |
| `chore` | Maintenance tasks (version bumps, file renames, etc.) |

**Examples:**

```
feat: add RCD demosaicing algorithm for Bayer pattern sensors

Implement the RCD (Ratio Corrected Demosaicing) algorithm as an
additional option in the Debayer tool. RCD produces fewer color
fringing artefacts than bilinear interpolation at comparable
computational cost.
```

```
fix: correct median calculation for single-channel float images

The statistical stretch routine was branching incorrectly for
monochrome 32-bit float images, producing a stretched output with
a black point of zero regardless of the configured shadow clipping.
```

---

## Pull Request Process

1. Ensure the build passes on your development platform with no new compiler warnings.
2. Update `BUILDING.md` if you have added or changed a build dependency.
3. Update `CONTRIBUTING.md` if you have changed a workflow or convention.
4. Add yourself to the contributors list in the pull request description if you wish.
5. Request a review from the maintainers. Do not self-merge.
6. Address all review feedback before the PR can be merged. Mark resolved comments as resolved.
7. Squash or rebase your branch onto the latest `main` before final merge if requested.

---

## Development Tips

- **IDE:** Qt Creator provides the most integrated experience for this project (CMake import,
  Qt-specific tooling, SIGNAL/SLOT navigation). VSCode with the CMake Tools and clangd extensions
  is also a viable option.
- **Debug symbols:** Always build with `CMAKE_BUILD_TYPE=Debug` during development. Release builds
  strip symbols and make debugging significantly harder.
- **Test image variety:** Use FITS 16-bit integer, FITS 32-bit float, multi-extension FITS, TIFF,
  and at least one camera RAW format when testing file I/O changes.
- **Thread safety:** The task and thread management system uses a priority work queue. When adding
  new background operations, use the existing `Task` and `TaskManager` infrastructure rather than
  raw `QThread` or `std::thread`.
- **Memory management:** Prefer RAII. Avoid raw `new`/`delete`. Use `std::unique_ptr` or
  `std::shared_ptr` for heap-allocated resources that are not managed by Qt's parent-child system.

---

## Questions?

Open a GitHub Discussion or file an issue tagged `question`. We are happy to help.