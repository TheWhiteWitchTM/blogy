#!/bin/sh
# Install Blogy for the current user. No sudo.
#   ./install.sh              install (or update)
#   ./install.sh --uninstall  remove it again (your ~/.config/blogy stays)
#
# Works from the bundled dist/ folder, or from the source tree (it builds
# and bundles first when dist/ is missing).
set -eu

APP=blogy
DATA="${XDG_DATA_HOME:-$HOME/.local/share}"
PREFIX="$DATA/$APP"
BIN="$HOME/.local/bin"
APPS="$DATA/applications"
ICONS="$DATA/icons/hicolor"
MARK="# blogy launcher (install.sh)"
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

refresh_caches() {
    command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database -q "$APPS" 2>/dev/null || true
    command -v gtk-update-icon-cache >/dev/null 2>&1 && gtk-update-icon-cache -q -t "$ICONS" 2>/dev/null || true
}

uninstall() {
    rm -rf "$PREFIX"
    if [ -f "$BIN/$APP" ] && grep -q "$MARK" "$BIN/$APP" 2>/dev/null; then
        rm -f "$BIN/$APP"
    fi
    rm -f "$APPS/$APP.desktop"
    for dir in "$ICONS"/*/apps; do
        rm -f "$dir/$APP.png" "$dir/$APP.svg"
    done
    refresh_caches
    echo "Blogy removed. Settings and secrets in ~/.config/blogy were left alone."
}

case "${1:-}" in
    --uninstall|-u) uninstall; exit 0 ;;
    --help|-h) sed -n '2,8p' "$0"; exit 0 ;;
    "") ;;
    *) echo "install.sh: unknown option $1" >&2; exit 2 ;;
esac

if [ -x "$HERE/bin/$APP" ]; then
    SRC="$HERE"
elif [ -x "$HERE/dist/bin/$APP" ]; then
    SRC="$HERE/dist"
elif [ -x "$HERE/scripts/build.sh" ]; then
    echo "No dist/ yet: building Blogy first."
    "$HERE/scripts/build.sh"
    "$HERE/scripts/bundle-dist.sh"
    SRC="$HERE/dist"
else
    echo "install.sh: run this from the Blogy dist folder or the source tree." >&2
    exit 1
fi

if [ "$SRC" != "$PREFIX" ]; then
    rm -rf "$PREFIX.new"
    mkdir -p "$(dirname "$PREFIX")"
    cp -a "$SRC" "$PREFIX.new"
    rm -rf "$PREFIX"
    mv "$PREFIX.new" "$PREFIX"
fi

mkdir -p "$BIN"
cat > "$BIN/$APP" << LAUNCH
#!/bin/sh
$MARK
# The bundled launcher sets LD_LIBRARY_PATH and QT_PLUGIN_PATH for its own Qt.
exec "$PREFIX/$APP" "\$@"
LAUNCH
chmod 0755 "$BIN/$APP"

if [ -d "$PREFIX/share/icons/hicolor" ]; then
    mkdir -p "$ICONS"
    cp -a "$PREFIX/share/icons/hicolor/." "$ICONS/"
fi
mkdir -p "$APPS"
# Desktop files do not expand ~, so write the absolute launcher path.
sed -e "s|^Exec=.*|Exec=$BIN/$APP %F|" -e "s|^Icon=.*|Icon=$APP|" \
    "$PREFIX/share/applications/$APP.desktop" > "$APPS/$APP.desktop"
chmod 0644 "$APPS/$APP.desktop"
refresh_caches

echo "Blogy installed to $PREFIX"
echo "Launcher: $BIN/$APP   (also in the app grid as Blogy)"
case ":$PATH:" in
    *":$BIN:"*) ;;
    *) echo "Note: $BIN is not on your PATH. Add this to ~/.profile, then log out and in:"
       echo "    export PATH=\"\$HOME/.local/bin:\$PATH\"" ;;
esac
echo "First start writes ~/.config/blogy/config.json. Add your keys there or in Settings."
