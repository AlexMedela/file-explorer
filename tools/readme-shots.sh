#!/bin/sh
# Regenerates docs/img/hero.png (the README screenshot) from a made-up home folder, so no personal files show.
# Needs ImageMagick and git. Usage: tools/readme-shots.sh [build/file-explorer]
set -e
cd "$(dirname "$0")/.."
BIN="${1:-build/file-explorer}"
D=/tmp/fe-readme; H=$D/home/alex
rm -rf "$D"
mkdir -p "$H/Documents" "$H/Downloads" "$H/Music" "$H/Pictures/Wallpapers" "$H/Projects/website/src" \
         "$H/Projects/website/assets" "$H/Projects/website/public" "$H/Videos" "$H/Desktop" "$D/cfg/file-explorer"
for f in "Budget 2026.ods" "Contract - signed.pdf" "Meeting notes.md" "Tax return 2025.pdf" "Thesis draft.odt"; do
  truncate -s 120000 "$H/Documents/$f"
done
i=0
for c in "#1e3a5f-#7aa2f7" "#2d1b4e-#f7768e" "#0f3d3e-#9ece6a" "#3b2a1a-#e0af68" "#1a1b26-#bb9af7" "#102030-#7dcfff"; do
  i=$((i + 1))
  magick -size 640x400 gradient:"${c%-*}"-"${c#*-}" -swirl $((i * 40)) "$H/Pictures/Wallpapers/wallpaper-$i.png"
done
( cd "$H/Projects/website"
  printf '# Website\n' > README.md; printf 'body{}\n' > src/style.css; printf '<html></html>\n' > src/index.html
  printf '{}\n' > package.json
  git init -q -b main; git add -A; git -c user.name=demo -c user.email=demo@example.com commit -qm init
  printf 'body{color:red}\n' > src/style.css
  for f in .gitignore LICENSE netlify.toml tsconfig.json vite.config.ts; do echo x > "$f"; done
  git add .gitignore LICENSE )
cat > "$D/cfg/file-explorer/settings.conf" <<CONF
window=1440x880
show_hidden=0
folder_sizes=1
restore_session=0
theme=Dark
icon_theme=Material
git_status=1
size_bars=1
CONF
printf '2 160 0 0 0 0\t%s\n' "$H/Pictures/Wallpapers" > "$D/cfg/file-explorer/folders.conf" # grid view
HOME=$H XDG_CONFIG_HOME=$D/cfg XDG_DATA_HOME=$D/data XDG_CACHE_HOME=$D/cache FE_TITLE_BAR=app \
  "$BIN" --screenshot "$D/hero.bmp" "$H/Projects/website" --tab "$H/Documents" --split "$H/Pictures/Wallpapers" >/dev/null 2>&1
mkdir -p docs/img
magick "$D/hero.bmp" docs/img/hero.png
echo "wrote docs/img/hero.png"
