#!/bin/bash
# =============================================================================
# TStar macOS Build Utilities
# =============================================================================
#
# Shared utility functions used by build_macos.sh, package_macos.sh, and
# build_installer_macos.sh.  This module centralises version detection,
# Homebrew / Qt / Python discovery, architecture handling, dynamic library
# copying and fixup, logging, and general file-system helpers.
#
# Usage:  source this file from the calling script.
# =============================================================================



# =============================================================================
# Section 1 -- Homebrew Utilities
# =============================================================================

# Returns the Homebrew installation prefix.
# Tries `brew --prefix` first, then falls back to well-known directory paths
# for Apple Silicon (/opt/homebrew) and Intel (/usr/local).
get_homebrew_prefix() {
    local prefix
    prefix=$(brew --prefix 2>/dev/null || echo "")

    if [ -z "$prefix" ]; then
        if [ -d "/opt/homebrew" ]; then
            prefix="/opt/homebrew"
        elif [ -d "/usr/local" ]; then
            prefix="/usr/local"
        fi
    fi

    echo "$prefix"
}

# Detects whether the current process is running under Rosetta (i386 on ARM).
# Returns 0 (true) if Rosetta is active, 1 (false) otherwise.
is_rosetta() {
    local arch
    arch=$(arch 2>/dev/null || echo "unknown")

    if [ "$arch" == "i386" ]; then
        return 0
    fi

    return 1
}


# =============================================================================
# Section 2 -- Qt Detection
# =============================================================================

# Locates the Qt 6 installation prefix through multiple strategies:
#   1. `brew --prefix qt@6`
#   2. `brew --prefix qt`
#   3. Direct Cellar search under /opt/homebrew
#   4. Intel fallback at /usr/local/opt/qt@6
detect_qt_prefix() {
    local qt_prefix=""

    # Strategy 1: Homebrew formula qt@6
    qt_prefix=$(brew --prefix qt@6 2>/dev/null || echo "")

    # Strategy 2: Homebrew formula qt (unversioned)
    if [ -z "$qt_prefix" ]; then
        qt_prefix=$(brew --prefix qt 2>/dev/null || echo "")
    fi

    # Strategy 3: Broken symlink -- search the Cellar directly
    if [ -z "$qt_prefix" ] || [ ! -d "$qt_prefix/bin" ]; then
        local cellar_qt
        cellar_qt=$(find /opt/homebrew/Cellar/qt* -maxdepth 2 -name "bin" -type d 2>/dev/null \
                     | head -1)
        if [ -n "$cellar_qt" ]; then
            qt_prefix=$(dirname "$cellar_qt")
        fi
    fi

    # Strategy 4: Intel fallback
    if [ -z "$qt_prefix" ] && [ -d "/usr/local/opt/qt@6" ]; then
        qt_prefix="/usr/local/opt/qt@6"
    fi

    echo "$qt_prefix"
}

# Locates the macdeployqt binary within a Qt installation.
# Accepts the Qt prefix as $1 and falls back to $PATH lookup.
find_macdeployqt() {
    local qt_prefix="$1"
    local macdeployqt=""

    if [ -f "$qt_prefix/bin/macdeployqt" ]; then
        macdeployqt="$qt_prefix/bin/macdeployqt"
    else
        macdeployqt=$(which macdeployqt 2>/dev/null || echo "")
    fi

    echo "$macdeployqt"
}


# =============================================================================
# Section 3 -- Architecture Detection
# =============================================================================

# Determines the build architecture for a given executable.
# When the executable is a universal binary the host architecture is preferred.
# Falls back to the native machine architecture when no file is provided.
detect_build_architecture() {
    local executable="$1"

    if [ -n "$executable" ] && [ -f "$executable" ]; then
        local file_output
        file_output=$(file "$executable" 2>/dev/null || echo "")

        local host_arch
        host_arch=$(uname -m)

        if [ "$host_arch" == "arm64" ]; then
            # Apple Silicon host -- prefer arm64 slice
            if echo "$file_output" | grep -q "arm64"; then
                echo "arm64"; return 0
            elif echo "$file_output" | grep -q "x86_64"; then
                echo "x86_64"; return 0
            fi
        else
            # Intel host -- prefer x86_64 slice
            if echo "$file_output" | grep -q "x86_64"; then
                echo "x86_64"; return 0
            elif echo "$file_output" | grep -q "arm64"; then
                echo "arm64"; return 0
            fi
        fi
    fi

    # Fallback: native machine architecture
    local native_arch
    native_arch=$(uname -m)

    if [ "$native_arch" == "arm64" ] || [ "$native_arch" == "aarch64" ]; then
        echo "arm64"
    else
        echo "x86_64"
    fi
}

# Checks whether a dynamic library contains the requested architecture.
# Universal (fat) binaries that include the target arch pass the check.
# Returns 0 on match, 1 on mismatch or missing file.
dylib_matches_arch() {
    local dylib="$1"
    local target_arch="$2"

    if [ ! -f "$dylib" ]; then
        return 1
    fi

    local file_output
    file_output=$(file "$dylib" 2>/dev/null || echo "")

    if echo "$file_output" | grep -q "$target_arch"; then
        return 0
    fi

    return 1
}


# =============================================================================
# Section 4 -- Recursive Dependency Copying
# =============================================================================

# Recursively copies all non-system, non-framework dylib dependencies of
# $1 into $2, filtering by target architecture $3.
# $4 is used internally to track already-processed libraries and prevent
# infinite recursion in circular dependency chains.
copy_dylib_with_dependencies() {
    local dylib="$1"
    local dest_dir="$2"
    local target_arch="$3"
    local processed_dylibs="${4:-}"

    # Guard against infinite loops on circular dependencies
    if echo "$processed_dylibs" | grep -q "$(basename "$dylib")"; then
        return 0
    fi
    processed_dylibs="$processed_dylibs $(basename "$dylib")"

    # Enumerate linked libraries (excluding the library's own header line)
    local deps
    deps=$(otool -L "$dylib" 2>/dev/null \
           | grep -v "^$dylib:" \
           | grep "\.dylib" \
           | awk '{print $1}' \
           | sort -u || true)

    for dep in $deps; do
        # Skip macOS system libraries
        if echo "$dep" | grep -qE "^(/usr/lib|/System|@executable_path)"; then
            continue
        fi

        # Skip framework references (handled separately by macdeployqt)
        if echo "$dep" | grep -q "\.framework"; then
            continue
        fi

        # Resolve @rpath references to a plain basename for searching
        local dep_basename
        if echo "$dep" | grep -q "@rpath"; then
            dep_basename=$(echo "$dep" | sed 's|@rpath/||')
        else
            dep_basename=$(basename "$dep")
        fi

        # Skip if already present in the destination
        if [ -f "$dest_dir/$dep_basename" ]; then
            continue
        fi

        # Attempt to locate and copy from Homebrew prefixes
        local found=0
        for brew_path in /opt/homebrew /usr/local; do

            # Method 1: Standard lib directory
            if [ -f "$brew_path/lib/$dep_basename" ]; then
                if dylib_matches_arch "$brew_path/lib/$dep_basename" "$target_arch"; then
                    cp -L "$brew_path/lib/$dep_basename" "$dest_dir/" 2>/dev/null && found=1
                    if [ $found -eq 1 ]; then
                        copy_dylib_with_dependencies "$dest_dir/$dep_basename" \
                            "$dest_dir" "$target_arch" "$processed_dylibs" || true
                    fi
                fi
                break
            fi

            # Method 2: Search opt/*/lib/ (covers openblas and similar keg-only formulae)
            if [ $found -eq 0 ] && [ -d "$brew_path/opt" ]; then
                local opt_match
                opt_match=$(find -L "$brew_path/opt" -maxdepth 3 -name "$dep_basename" \
                            2>/dev/null | head -1)
                if [ -n "$opt_match" ]; then
                    if dylib_matches_arch "$opt_match" "$target_arch"; then
                        cp -L "$opt_match" "$dest_dir/" 2>/dev/null && found=1
                        if [ $found -eq 1 ]; then
                            copy_dylib_with_dependencies "$dest_dir/$dep_basename" \
                                "$dest_dir" "$target_arch" "$processed_dylibs" || true
                        fi
                    fi
                fi
            fi

            # Method 3: Search Cellar directly for versioned paths
            if [ $found -eq 0 ] && [ -d "$brew_path/Cellar" ]; then
                local cellar_match
                cellar_match=$(find -L "$brew_path/Cellar" -maxdepth 5 -name "$dep_basename" \
                               2>/dev/null | head -1)
                if [ -n "$cellar_match" ]; then
                    if dylib_matches_arch "$cellar_match" "$target_arch"; then
                        cp -L "$cellar_match" "$dest_dir/" 2>/dev/null && found=1
                        if [ $found -eq 1 ]; then
                            copy_dylib_with_dependencies "$dest_dir/$dep_basename" \
                                "$dest_dir" "$target_arch" "$processed_dylibs" || true
                        fi
                    fi
                fi
            fi
        done

        # Method 4: Try the original absolute path (only for non-@rpath references)
        if [ $found -eq 0 ] && ! echo "$dep" | grep -q "@rpath"; then
            if [ -f "$dep" ]; then
                if dylib_matches_arch "$dep" "$target_arch"; then
                    cp -L "$dep" "$dest_dir/" 2>/dev/null && found=1
                    if [ $found -eq 1 ]; then
                        copy_dylib_with_dependencies "$dest_dir/$dep_basename" \
                            "$dest_dir" "$target_arch" "$processed_dylibs" || true
                    fi
                fi
            fi
        fi

        # Silently ignore well-known system libraries that are not packaged
        if [ $found -eq 0 ] && ! echo "$dep" | grep -qE "libSystem\.B|libobjc\.A|libstdc|libc\+\+"; then
            true
        fi
    done
}


# =============================================================================
# Section 5 -- High-Level Dependency Copy (copy_dylib)
# =============================================================================

# Copies a named Homebrew dylib into the app bundle Frameworks directory.
#
# Arguments:
#   $1  lib_name     -- Library base name (e.g. "libgsl")
#   $2  brew_pkg     -- Homebrew formula name (e.g. "gsl")
#   $3  dest_dir     -- Destination directory (typically Contents/Frameworks)
#   $4  target_arch  -- (Optional) Target architecture override
#
# The function searches multiple Homebrew prefixes, validates architecture
# compatibility, and prints a diagnostic message indicating success or failure.
copy_dylib() {
    local lib_name="$1"
    local brew_pkg="$2"
    local dest_dir="$3"
    local target_arch="${4:-}"

    # Auto-detect target architecture from the app executable if not supplied
    if [ -z "$target_arch" ]; then
        if [ -d "$dest_dir/../MacOS" ]; then
            local executable
            executable=$(find "$dest_dir/../MacOS" -name "TStar" -o -name "*.app" 2>/dev/null \
                         | head -1)
            if [ -z "$executable" ]; then
                executable=$(find "$dest_dir/../MacOS" -type f -executable 2>/dev/null | head -1)
            fi
            if [ -n "$executable" ]; then
                target_arch=$(detect_build_architecture "$executable")
            fi
        fi
    fi

    # Ultimate fallback: native host architecture
    if [ -z "$target_arch" ]; then
        target_arch=$(detect_build_architecture "")
    fi

    # Build an ordered list of search paths based on the target architecture
    local search_paths=()
    if [ "$target_arch" == "x86_64" ]; then
        search_paths=("/usr/local" "/opt/homebrew")
    else
        search_paths=("/opt/homebrew" "/usr/local")
    fi

    # Prepend the Homebrew-reported prefix for the specific formula
    local brew_prefix
    brew_prefix=$(brew --prefix "$brew_pkg" 2>/dev/null || echo "")
    if [ -n "$brew_prefix" ]; then
        search_paths=("$brew_prefix" "${search_paths[@]}")
    fi

    # Iterate through search paths looking for a matching dylib
    local found_any_arch=""
    local checked_paths=""

    for base_path in "${search_paths[@]}"; do
        if [ ! -d "$base_path" ]; then continue; fi
        checked_paths="$checked_paths $base_path"

        local candidates=()

        # Method A: Standard library path
        if [ -d "$base_path/lib" ]; then
            local found
            found=$(find -L "$base_path/lib" -name "${lib_name}*.dylib" -maxdepth 2 2>/dev/null \
                    | grep -v ".dSYM" | sort)
            if [ -n "$found" ]; then candidates+=($found); fi
        fi

        # Method B: Cellar versioned paths
        if [ -d "$base_path/Cellar/$brew_pkg" ]; then
            local found
            found=$(find -L "$base_path/Cellar/$brew_pkg" -name "${lib_name}*.dylib" \
                    -maxdepth 6 2>/dev/null | grep -v ".dSYM" | sort)
            if [ -n "$found" ]; then candidates+=($found); fi
        fi

        # Method C: Aggressive recursive search (last resort)
        if [ -z "$found_any_arch" ] && [ ${#candidates[@]} -eq 0 ]; then
            local name_pattern="${lib_name}*.dylib"
            if [ "$brew_pkg" == "libraw" ]; then name_pattern="*raw*.dylib"; fi

            local found
            found=$(find -L "$base_path" -maxdepth 8 -name "$name_pattern" 2>/dev/null \
                    | grep -v ".dSYM" | sort)
            if [ -n "$found" ]; then candidates+=($found); fi
        fi

        # Evaluate each candidate for architecture compatibility
        for dylib in "${candidates[@]}"; do
            if [ -f "$dylib" ]; then
                if dylib_matches_arch "$dylib" "$target_arch"; then
                    # Copy with dereferenced symlinks and canonical base name
                    cp -L "$dylib" "$dest_dir/${lib_name}.dylib" 2>/dev/null

                    # Also keep the original versioned name for safety
                    if [ "$(basename "$dylib")" != "${lib_name}.dylib" ]; then
                        cp -L "$dylib" "$dest_dir/" 2>/dev/null
                    fi

                    echo "  - $lib_name: OK ($target_arch) [from: $base_path]"
                    return 0
                else
                    # Record the incompatible architecture for the error message
                    local found_arch
                    found_arch=$(file "$dylib" 2>/dev/null \
                                 | grep -o "x86_64\|arm64" | head -1)
                    found_any_arch="$found_arch"
                fi
            fi
        done
    done

    # Report failure with actionable diagnostics
    if [ -n "$found_any_arch" ]; then
        echo "  - $lib_name: ARCH MISMATCH (target: $target_arch, found: $found_any_arch in checked paths)"
        if [ "$target_arch" == "x86_64" ] && [ "$found_any_arch" == "arm64" ]; then
            echo "    [TIP] Try: arch -x86_64 brew install $brew_pkg"
        fi
    else
        echo "  - $lib_name: NOT FOUND in paths: $checked_paths"
        echo "    [TIP] If building for arm64, ensure 'brew install $brew_pkg' was run."
    fi
    return 1
}


# =============================================================================
# Section 6 -- Library Install-Name Fixup
# =============================================================================

# Rewrites the install name ID and dependency paths of a single dylib so that
# all references use @rpath-relative paths rather than absolute Homebrew paths.
fix_dylib_id_and_deps() {
    local dylib_path="$1"
    local frameworks_dir="$2"

    if [ ! -f "$dylib_path" ]; then
        return
    fi

    chmod +w "$dylib_path"

    local dylib_name
    dylib_name=$(basename "$dylib_path")

    # Set the library's own install name to @rpath/<name>
    install_name_tool -id "@rpath/$dylib_name" "$dylib_path" 2>/dev/null || true

    # Rewrite each dependency reference
    local deps
    deps=$(otool -L "$dylib_path" 2>/dev/null \
           | grep -v "^$dylib_path:" \
           | awk '{print $1}')

    for dep in $deps; do
        # Leave system and already-correct references untouched
        if echo "$dep" | grep -qE "^(/usr/lib|/System)"; then
            continue
        fi
        if echo "$dep" | grep -q "@executable_path"; then
            continue
        fi

        local dep_name
        dep_name=$(basename "$dep")

        if [ -f "$frameworks_dir/$dep_name" ]; then
            # Dependency is bundled -- rewrite to @rpath
            if [ "$dep" != "@rpath/$dep_name" ]; then
                install_name_tool -change "$dep" "@rpath/$dep_name" "$dylib_path" 2>/dev/null || true
            fi
        elif echo "$dep" | grep -qE "^(/opt/homebrew|/usr/local/(Cellar|opt|lib))"; then
            # Absolute Homebrew path that will not exist on end-user machines
            install_name_tool -change "$dep" "@rpath/$dep_name" "$dylib_path" 2>/dev/null || true
        fi
    done
}

# Rewrites dependency paths inside the main application executable so that
# bundled libraries are loaded via @rpath instead of absolute paths.
fix_executable_deps() {
    local exec_path="$1"
    local frameworks_dir="$2"

    if [ ! -f "$exec_path" ] || [ ! -d "$frameworks_dir" ]; then
        return
    fi

    chmod +w "$exec_path"

    local deps
    deps=$(otool -L "$exec_path" 2>/dev/null \
           | grep -v "^$exec_path:" \
           | awk '{print $1}')

    for dep in $deps; do
        if echo "$dep" | grep -qE "^(/usr/lib|/System)"; then
            continue
        fi
        if echo "$dep" | grep -q "@executable_path"; then
            continue
        fi

        local dep_name
        dep_name=$(basename "$dep")

        if [ -f "$frameworks_dir/$dep_name" ]; then
            if [ "$dep" != "@rpath/$dep_name" ]; then
                install_name_tool -change "$dep" "@rpath/$dep_name" "$exec_path" 2>/dev/null || true
                echo "  - Repointed $dep_name to bundled version"
            fi
        elif echo "$dep" | grep -qE "^(/opt/homebrew|/usr/local/(Cellar|opt|lib))"; then
            install_name_tool -change "$dep" "@rpath/$dep_name" "$exec_path" 2>/dev/null || true
            echo "  - Repointed $dep_name (Homebrew absolute path) to @rpath"
        fi
    done
}

# Final safety-net sweep: scans a binary and rewrites every remaining
# /opt/homebrew/ or /usr/local/Cellar|opt|lib reference to @rpath/<basename>.
# Also fixes the binary's own ID if it contains an absolute Homebrew path.
rewrite_homebrew_paths() {
    local binary_path="$1"

    if [ ! -f "$binary_path" ]; then
        return
    fi

    chmod +w "$binary_path" 2>/dev/null || true

    # Fix the binary's own install name ID (prevents QtDBus warnings)
    local binary_id
    binary_id=$(otool -D "$binary_path" 2>/dev/null | sed -n '2p' | awk '{print $1}')
    if echo "$binary_id" | grep -qE "^(/opt/homebrew|/usr/local/(Cellar|opt|lib))"; then
        install_name_tool -id "@rpath/$(basename "$binary_path")" "$binary_path" 2>/dev/null || true
    fi

    # Rewrite dependency references
    local deps
    deps=$(otool -L "$binary_path" 2>/dev/null \
           | grep -v "^$binary_path:" \
           | awk '{print $1}')
    local rewrote=0

    for dep in $deps; do
        if echo "$dep" | grep -qE "^(/opt/homebrew|/usr/local/(Cellar|opt|lib))"; then
            local dep_name
            dep_name=$(basename "$dep")
            install_name_tool -change "$dep" "@rpath/$dep_name" "$binary_path" 2>/dev/null || true
            rewrote=$((rewrote + 1))
        fi
    done

    if [ $rewrote -gt 0 ]; then
        echo "  - Rewrote $rewrote Homebrew path(s) in $(basename "$binary_path")"
    fi
}


# =============================================================================
# Section 7 -- Python Detection
# =============================================================================

# Locates a compatible Python interpreter (3.11 -- 3.13).
# Search order: versioned commands in PATH, Homebrew installations, generic python3.
find_compatible_python() {
    local COMPAT_VERSIONS=("3.13" "3.12" "3.11")
    local PYTHON_CMD=""
    local HOMEBREW_PREFIX
    HOMEBREW_PREFIX=$(get_homebrew_prefix)

    # Attempt 1: Versioned commands available in PATH
    for ver in "${COMPAT_VERSIONS[@]}"; do
        if command -v "python$ver" &> /dev/null; then
            PYTHON_CMD="python$ver"
            break
        fi
    done

    # Attempt 2: Homebrew-installed Python
    if [ -z "$PYTHON_CMD" ]; then
        for ver in "${COMPAT_VERSIONS[@]}"; do
            local brew_python="$HOMEBREW_PREFIX/opt/python@$ver/bin/python$ver"
            if [ -x "$brew_python" ]; then
                PYTHON_CMD="$brew_python"
                break
            fi
        done
    fi

    # Attempt 3: Generic python3 (validated against supported version range)
    if [ -z "$PYTHON_CMD" ] && command -v python3 &> /dev/null; then
        local p3_ver
        p3_ver=$(python3 -c \
            'import sys; print(f"{sys.version_info.major}.{sys.version_info.minor}")' 2>/dev/null)
        local p3_major p3_minor
        p3_major=$(echo "$p3_ver" | cut -d. -f1)
        p3_minor=$(echo "$p3_ver" | cut -d. -f2)

        if [ "$p3_major" -eq 3 ] && [ "$p3_minor" -ge 11 ] && [ "$p3_minor" -le 13 ]; then
            PYTHON_CMD="python3"
        fi
    fi

    echo "$PYTHON_CMD"
}

# =============================================================================
# Section 8 -- Validation Helpers
# =============================================================================

# Returns 0 if the given command is available on the system, 1 otherwise.
check_command() {
    local cmd="$1"
    if ! command -v "$cmd" &> /dev/null; then
        return 1
    fi
    return 0
}
