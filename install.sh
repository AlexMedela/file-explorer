#!/bin/sh
# Install File Explorer. Usage: [DESTDIR=...] [PREFIX=/usr/local] ./install.sh
# Build first with ./build.sh (or ./build.sh package for a portable binary).
set -e
cd "$(dirname "$0")"
PREFIX="${PREFIX:-/usr/local}"
DEST="${DESTDIR:-}"
[ -x build/file-explorer ] || { echo "build/file-explorer missing: run ./build.sh first"; exit 1; }
install -Dm755 build/file-explorer "$DEST$PREFIX/bin/file-explorer"
install -Dm644 packaging/dev.fileexplorer.desktop "$DEST$PREFIX/share/applications/dev.fileexplorer.desktop"
sed "s|@BINDIR@|$PREFIX/bin|" packaging/dev.fileexplorer.policy > /tmp/fe-policy.$$ # "Retry as administrator" (pkexec)
install -Dm644 /tmp/fe-policy.$$ "$DEST$PREFIX/share/polkit-1/actions/dev.fileexplorer.policy"
rm -f /tmp/fe-policy.$$
# phones and cameras over USB for the user at the seat (udev only reads /usr/lib and /etc)
UDEV_DIR="$PREFIX/lib/udev/rules.d"; [ "$PREFIX" = /usr ] || UDEV_DIR=/etc/udev/rules.d
install -Dm644 packaging/udev/70-file-explorer-phones.rules "$DEST$UDEV_DIR/70-file-explorer-phones.rules"
install -Dm644 packaging/dev.fileexplorer.svg "$DEST$PREFIX/share/icons/hicolor/scalable/apps/dev.fileexplorer.svg"
install -Dm644 README.md "$DEST$PREFIX/share/doc/file-explorer/README.md"
for d in docs/*.md; do install -Dm644 "$d" "$DEST$PREFIX/share/doc/file-explorer/$d"; done
install -Dm644 LICENSE "$DEST$PREFIX/share/licenses/file-explorer/LICENSE"
install -Dm644 third_party/stb_image_LICENSE.txt "$DEST$PREFIX/share/licenses/file-explorer/stb_image_LICENSE.txt"
install -Dm644 third_party/nanosvg_LICENSE.txt "$DEST$PREFIX/share/licenses/file-explorer/nanosvg_LICENSE.txt"
install -Dm644 third_party/material-icons/LICENSE "$DEST$PREFIX/share/licenses/file-explorer/material-icon-theme_LICENSE.txt"
install -Dm644 third_party/icons-catppuccin/LICENSE "$DEST$PREFIX/share/licenses/file-explorer/catppuccin-icons_LICENSE.txt"
install -Dm644 third_party/icons-symbols/LICENSE "$DEST$PREFIX/share/licenses/file-explorer/symbols-icons_LICENSE.txt"
echo "installed to $DEST$PREFIX"
if [ -z "$DEST" ]; then
  command -v update-desktop-database >/dev/null && update-desktop-database -q "$PREFIX/share/applications" || true
  command -v gtk-update-icon-cache >/dev/null && gtk-update-icon-cache -q -t "$PREFIX/share/icons/hicolor" || true
  command -v udevadm >/dev/null && udevadm control --reload 2>/dev/null || true
  echo "To make it the default folder handler: xdg-mime default dev.fileexplorer.desktop inode/directory"
fi
