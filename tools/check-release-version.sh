#!/bin/sh
# Check all checked-in release versions; optionally check a v-prefixed tag.
set -eu
cd "$(dirname "$0")/.."

version=$(sed -n 's/^#define APP_VERSION "\([^"]*\)".*/\1/p' src/app.c)
spec=$(sed -n 's/^Version:[[:space:]]*\([^[:space:]]*\).*/\1/p' packaging/obs/file-explorer.spec)
dsc=$(sed -n 's/^Version:[[:space:]]*\([^[:space:]]*\).*/\1/p' packaging/obs/file-explorer.dsc)
deb=$(sed -n '1s/^file-explorer (\([^)]*\)).*/\1/p' packaging/obs/debian.changelog)

case "$dsc" in "$version"-[1-9]*) valid_debian=true ;; *) valid_debian=false ;; esac
if [ -z "$version" ] || [ "$spec" != "$version" ] || [ "$valid_debian" != true ] ||
   [ "$deb" != "$dsc" ] || ! grep -Fxq "## $version" CHANGELOG.md; then
    echo "Release versions disagree: app=$version spec=$spec dsc=$dsc debian=$deb" >&2
    exit 1
fi
if [ "$#" -gt 0 ] && [ "$1" != "v$version" ]; then
    echo "Tag $1 does not match v$version" >&2
    exit 1
fi
echo "Release versions match: $version"
