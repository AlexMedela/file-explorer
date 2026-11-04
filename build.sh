#!/bin/sh
# Build File Explorer. Usage: ./build.sh [release|debug|package]
# FE_ADDON_SRC=<dir>: compile in the add-on code from <dir> (see src/addons.c); FE_OUT=<file>: where the binary goes.
# Both absolute paths.
set -e
cd "$(dirname "$0")"
mode="${1:-release}"
out="${FE_OUT:-build/file-explorer}"
mkdir -p "$(dirname "$out")"
ADDON=""
SRCS="src/*.c src/*.h"
if [ -n "$FE_ADDON_SRC" ]; then ADDON="-DFE_ADDON -I$FE_ADDON_SRC"; SRCS="$SRCS $FE_ADDON_SRC/*.c"; fi
PKGS="sdl3 freetype2 fontconfig libarchive vterm fuse3 openssl"
CFLAGS="-std=gnu11 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -Wno-format-truncation $(pkg-config --cflags $PKGS)"
LIBS="$(pkg-config --libs $PKGS) -lpthread -lm"
case "$mode" in
  release) OPT="-O2 -march=native -DNDEBUG -flto=auto" ;;
  package) OPT="${CFLAGS_PKG:--O2} -DNDEBUG" ;;   # portable: no -march=native (distro packages)
  debug)   OPT="-O0 -g3 -fsanitize=address,undefined -fno-omit-frame-pointer" ;;
  *) echo "unknown mode: $mode"; exit 1 ;;
esac
# Source must be pure ASCII: UTF-8 text goes in as \x escapes (catches mangled characters).
# shellcheck disable=SC2086
if LC_ALL=C grep -nP '[^\x00-\x7F]' $SRCS; then echo "error: non-ASCII characters in source (use \\x escapes)"; exit 1; fi
${CC:-cc} $CFLAGS $ADDON $OPT src/main.c -o "$out" $LIBS ${LDFLAGS_PKG:-}
echo "built $out ($mode${ADDON:+, with add-ons})"
