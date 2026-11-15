#!/bin/sh
# Sync the checked-in package metadata after bumping APP_VERSION and CHANGELOG.md.
set -eu
cd "$(dirname "$0")/.."

version=$(sed -n 's/^#define APP_VERSION "\([^"]*\)".*/\1/p' src/app.c)
if [ -z "$version" ] || ! grep -Fxq "## $version" CHANGELOG.md; then
    echo "Set APP_VERSION and add a matching CHANGELOG.md section first" >&2
    exit 1
fi

sed -i "s/^Version:[[:space:]]*.*/Version:        $version/" packaging/obs/file-explorer.spec
debian_version=$(sed -n '1s/^file-explorer (\([^)]*\)).*/\1/p' packaging/obs/debian.changelog)
case "$debian_version" in
    "$version"-[1-9]*) ;;
    *) debian_version="$version-1" ;;
esac
sed -i "s/^Version:[[:space:]]*.*/Version: $debian_version/" packaging/obs/file-explorer.dsc

if ! head -n 1 packaging/obs/debian.changelog | grep -Fxq "file-explorer ($debian_version) unstable; urgency=medium"; then
    tmp=$(mktemp)
    trap 'rm -f "$tmp"' EXIT HUP INT TERM
    {
        printf 'file-explorer (%s) unstable; urgency=medium\n\n' "$debian_version"
        printf '  * Release %s.\n\n' "$version"
        printf ' -- AlexMedela <70584290+AlexMedela@users.noreply.github.com>  %s\n\n' "$(date -R)"
        cat packaging/obs/debian.changelog
    } > "$tmp"
    cat "$tmp" > packaging/obs/debian.changelog
fi

sh tools/check-release-version.sh
