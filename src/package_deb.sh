#!/bin/bash
# =============================================================================
# TStar Debian Package Build Script
# This script builds a .deb package for TStar on Debian-based Linux distributions.
# It assumes that the project has already been built using build_linux.sh.
# =============================================================================

cp_all() {
    local SRC="$1"
    local DEST="$2"
    for FILE in $SRC; do
        cp "$FILE" "$DEST"
    done
}

set -e

# Check for silent mode
SILENT_MODE=0
if [ "$1" == "--silent" ]; then
    SILENT_MODE=1
fi

if [ $SILENT_MODE -eq 0 ]; then
    echo "==========================================="
    echo " TStar Distribution Packager (Debian)"
    echo "==========================================="
    echo ""
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR/.."
PROJECT_ROOT="$(pwd)"

# Load utilities
if [ -f "$SCRIPT_DIR/utils_common.sh" ]; then
    source "$SCRIPT_DIR/utils_common.sh"
else
    echo "[ERROR] utils_common.sh not found!"
    exit 1
fi

get_arch() {
    local ARCH=$(uname -m)
    case "$ARCH" in
        x86_64) echo "amd64" ;;
        aarch64) echo "arm64" ;;
        *) echo "unknown" ;;
    esac
}

ARCH=$(get_arch)
VERSION=$(get_version)
if [ $SILENT_MODE -eq 0 ]; then
    log_info "Packaging version $VERSION for $ARCH"
fi

BUILD_DIR="build"
DIST_DIR="dist/tstar_${VERSION}_${ARCH}"

# --- Verify build exists ---
echo ""
log_step 1 "Verifying build..."

verify_file "$BUILD_DIR/TStar" "TStar app" || {
    echo "Please run ./src/build_linux.sh first."
    exit 1
}
echo "  - TStar app: OK"

# --- Clean old dist ---
echo ""
log_step 2 "Preparing distribution folder..."

safe_rm_rf "dist"
ensure_dir "dist"

# --- Copy files ---
echo ""
log_step 3 "Copying files..."
ensure_dir "$DIST_DIR/opt/tstar"

cp "$BUILD_DIR/TStar" "$DIST_DIR/opt/tstar/"
chmod 755 "$DIST_DIR/opt/tstar/TStar"

cp -r "$PROJECT_ROOT/deps/python_venv" "$DIST_DIR/opt/tstar/python_venv"

cp_all "$BUILD_DIR/*.qm" "$DIST_DIR/opt/tstar/"
cp -r "$BUILD_DIR/data" "$DIST_DIR/opt/tstar/"
cp -r "$BUILD_DIR/images" "$DIST_DIR/opt/tstar/"
cp -r "$BUILD_DIR/scripts" "$DIST_DIR/opt/tstar/"
cp_all "$PROJECT_ROOT/src/scripts/*" "$DIST_DIR/opt/tstar/scripts/"
if [ -d "$BUILD_DIR/translations" ]; then
    cp -r "$BUILD_DIR/translations" "$DIST_DIR/opt/tstar/"
fi
ASTAP_DIR="$PROJECT_ROOT/deps/astap"
if [ -d "$ASTAP_DIR" ]; then
    cp -r "$ASTAP_DIR" "$DIST_DIR/opt/tstar/"
fi

ensure_dir "$DIST_DIR/usr/share/doc/tstar/"
cp "$PROJECT_ROOT/LICENSE" "$DIST_DIR/usr/share/doc/tstar/copyright"

if ! command -v convert >/dev/null 2>&1; then
    log_error "ImageMagick 'convert' command not found. Install ImageMagick to continue."
    exit 1
fi

ICONS_DIR="$DIST_DIR/usr/share/icons/hicolor"
ensure_dir "$ICONS_DIR"
ensure_dir "$ICONS_DIR/16x16/apps"
ensure_dir "$ICONS_DIR/32x32/apps"
ensure_dir "$ICONS_DIR/48x48/apps"
ensure_dir "$ICONS_DIR/64x64/apps"
ensure_dir "$ICONS_DIR/128x128/apps"
ensure_dir "$ICONS_DIR/128x128@2x/apps"
ensure_dir "$ICONS_DIR/256x256/apps"
ensure_dir "$ICONS_DIR/256x256@2x/apps"
ensure_dir "$ICONS_DIR/512x512/apps"
ensure_dir "$ICONS_DIR/512x512@2x/apps"
convert "$BUILD_DIR/images/Logo.png" -resize 16x16 "$ICONS_DIR/16x16/apps/tstar.png"
convert "$BUILD_DIR/images/Logo.png" -resize 32x32 "$ICONS_DIR/32x32/apps/tstar.png"
convert "$BUILD_DIR/images/Logo.png" -resize 48x48 "$ICONS_DIR/48x48/apps/tstar.png"
convert "$BUILD_DIR/images/Logo.png" -resize 64x64 "$ICONS_DIR/64x64/apps/tstar.png"
convert "$BUILD_DIR/images/Logo.png" -resize 128x128 "$ICONS_DIR/128x128/apps/tstar.png"
convert "$BUILD_DIR/images/Logo.png" -resize 256x256 "$ICONS_DIR/128x128@2x/apps/tstar.png"
convert "$BUILD_DIR/images/Logo.png" -resize 256x256 "$ICONS_DIR/256x256/apps/tstar.png"
convert "$BUILD_DIR/images/Logo.png" -resize 512x512 "$ICONS_DIR/256x256@2x/apps/tstar.png"
convert "$BUILD_DIR/images/Logo.png" -resize 512x512 "$ICONS_DIR/512x512/apps/tstar.png"
convert "$BUILD_DIR/images/Logo.png" -resize 1024x1024 "$ICONS_DIR/512x512@2x/apps/tstar.png"

# --- Create symlink to executable ---
ensure_dir "$DIST_DIR/usr/bin"
ln -s "../../opt/tstar/TStar" "$DIST_DIR/usr/bin/TStar"

# --- Create debian control file ---
echo ""
log_step 4 "Creating debian control file..."

cd "$DIST_DIR"
mkdir -p debian
echo "Source: placeholder" > debian/control
DEPENDENCIES=$(dpkg-shlibdeps usr/bin/TStar -O 2>/dev/null | sed -e 's/shlibs:Depends=//g')
rm -rf debian
cd -

DEBIAN_DIR="$DIST_DIR/DEBIAN"
ensure_dir "$DEBIAN_DIR"
chmod 755 "$DEBIAN_DIR"
CONTROL_FILE="$DEBIAN_DIR/control"

cat > "$CONTROL_FILE" <<EOL
Package: tstar
Version: $VERSION
Maintainer: Ft2801 <fabiot2801@gmail.com>
Section: science
Priority: optional
Architecture: $ARCH
Depends: $DEPENDENCIES
Description: A professional astronomy software and astro editing app
EOL
chmod 644 "$CONTROL_FILE"

# --- Create desktop entry ---
echo ""
log_step 5 "Creating desktop entry..."
DESKTOP_DIR="$DIST_DIR/usr/share/applications"
ensure_dir "$DESKTOP_DIR"
cat > "$DESKTOP_DIR/tstar.desktop" <<EOL
[Desktop Entry]
Name=TStar
Comment=A professional astronomy software and astro editing app
Exec=/usr/bin/TStar %U
Try-Exec=/usr/bin/TStar
Icon=tstar
Terminal=false
Type=Application
MimeType=application/x-tstar-project;
Categories=Science;Astronomy;
EOL
chmod 644 "$DESKTOP_DIR/tstar.desktop"

MIME_DIR="$DIST_DIR/usr/share/mime/packages"
ensure_dir "$MIME_DIR"
cat > "$MIME_DIR/tstar.xml" <<EOL
<?xml version="1.0" encoding="UTF-8"?>
<mime-info xmlns="http://www.freedesktop.org/standards/shared-mime-info">
    <mime-type type="application/x-tstar-project">
        <comment>TStar Project File</comment>
        <glob pattern="*.tstarproj"/>
    </mime-type>
</mime-info>
EOL
chmod 644 "$MIME_DIR/tstar.xml"

POSTINIT_SCRIPT="$DEBIAN_DIR/postinst"
cat > "$POSTINIT_SCRIPT" <<EOL
#!/bin/bash
set -e
update-mime-database /usr/share/mime
update-desktop-database /usr/share/applications
EOL
chmod 755 "$POSTINIT_SCRIPT"

# --- Build the .deb package ---
echo ""
log_step 6 "Building .deb package..."
dpkg-deb --build --root-owner-group "$DIST_DIR" > /dev/null

if [ $SILENT_MODE -eq 0 ]; then
    log_info "Package created"
fi