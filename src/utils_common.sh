
# --- File management ---
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

# --- Validation ---


verify_file() {
    local file="$1"
    local description="$2"

    if [ ! -f "$file" ]; then
        log_error "$description not found: $file"
        return 1
    fi
    return 0
}

verify_dir() {
    local dir="$1"
    local description="$2"

    if [ ! -d "$dir" ]; then
        log_error "$description not found: $dir"
        return 1
    fi
    return 0
}

# --- Logging Functions ---

log_info() {
    echo "[INFO] $1"
}

log_error() {
    echo "[ERROR] $1" >&2
}

log_warning() {
    echo "[WARNING] $1"
}

log_step() {
    echo ""
    echo "[STEP $1] $2"
}

# --- Version Management ---
get_version() {
    local VERSION="1.0.0"
    if [ -f "changelog.txt" ]; then
        VERSION=$(grep -E "^Version [0-9.]+" changelog.txt | head -1 | awk '{print $2}' | tr -d '\r')
        if [ -z "$VERSION" ]; then
            VERSION="1.0.0"
        fi
    fi
    echo "$VERSION"
}