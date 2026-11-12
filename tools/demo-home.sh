#!/bin/sh
# Build the made-up home folder the demo video is recorded in (BASE/home, default /tmp/fe-demo), from scratch,
# and a stand-in lsblk so the sidebar shows made-up drives instead of this machine's (BASE/bin).
# Usage: demo-home.sh [BASE]   Needs git, magick (ImageMagick), zip, python3.
set -e
repo="$(cd "$(dirname "$0")/.." && pwd)"
B="${1:-/tmp/fe-demo}"
H="$B/home"
rm -rf "$H"
mkdir -p "$H" "$B/bin"
cat > "$B/bin/lsblk" <<'LSBLK'
#!/bin/sh
echo 'PATH="/dev/sdx" PKNAME="" TYPE="disk" RM="1" HOTPLUG="1" MAJ:MIN="8:224" LABEL="" FSTYPE="" SIZE="64000000000" PARTTYPE=""'
echo 'PATH="/dev/sdx1" PKNAME="sdx" TYPE="part" RM="1" HOTPLUG="1" MAJ:MIN="8:225" LABEL="USB\x20Stick" FSTYPE="exfat" SIZE="63999000000" PARTTYPE="0x7"'
echo 'PATH="/dev/sdw1" PKNAME="sdw" TYPE="part" RM="0" HOTPLUG="0" MAJ:MIN="8:209" LABEL="Data" FSTYPE="ext4" SIZE="2000000000000" PARTTYPE=""'
LSBLK
chmod +x "$B/bin/lsblk"
cd "$H"
mkdir -p Desktop Documents Downloads Music Pictures/Wallpapers Pictures/Screenshots Videos Projects Backup Datasets Public Templates

# Pictures: soft gradients with two blurred blobs (name, background from/to, blob colours, blob centres)
img() {
    magick -size 960x600 -define gradient:angle=135 gradient:"$2-$3" \
        \( -size 960x600 xc:none -fill "$4" -draw "circle $6,$7 $(($6 + 230)),$7" -blur 0x110 \) -composite \
        \( -size 960x600 xc:none -fill "$5" -draw "circle $8,$9 $(($8 + 200)),$9" -blur 0x100 \) -composite \
        -attenuate 0.15 +noise gaussian "Pictures/Wallpapers/$1.jpg"
}
img aurora "#0b1023" "#123b4a" "#22d3ee" "#a78bfa" 250 420 700 150
img dunes "#3b1d0e" "#b45309" "#fbbf24" "#f97316" 650 350 220 120
img glacier "#0c4a6e" "#e0f2fe" "#7dd3fc" "#ffffff" 300 200 750 450
img lagoon "#042f2e" "#0d9488" "#5eead4" "#22c55e" 700 400 200 200
img nebula "#1e1b4b" "#4c1d95" "#ec4899" "#6366f1" 300 300 720 220
img canyon "#450a0a" "#9a3412" "#fb923c" "#dc2626" 200 450 700 200
img meadow "#14532d" "#65a30d" "#bef264" "#facc15" 650 180 250 450
img ember "#18181b" "#7f1d1d" "#f97316" "#facc15" 480 420 200 150
img tide "#082f49" "#1d4ed8" "#38bdf8" "#e879f9" 250 180 700 420
img frost "#e0e7ff" "#a5b4fc" "#ffffff" "#818cf8" 700 200 250 400
img dusk "#312e81" "#be185d" "#fb7185" "#fcd34d" 480 500 200 150
img orchid "#3b0764" "#a21caf" "#f0abfc" "#c084fc" 250 250 720 400
n=0
for f in nebula lagoon dusk tide; do n=$((n + 1)); magick "Pictures/Wallpapers/$f.jpg" -resize 640x400 "Pictures/Screenshots/Screenshot_2026-11-0$n.png"; done
cp Pictures/Wallpapers/aurora.jpg Desktop/wallpaper.jpg

# Documents
printf '# Trip to Lisbon\n\n- Flights: 14 Nov, 08:40\n- Hotel: Alfama, 3 nights\n- Tram 28, Belem, LX Factory\n' > Documents/lisbon-trip.md
printf 'Quarter,Revenue,Costs\nQ1,120400,80100\nQ2,134900,82300\nQ3,151200,90800\nQ4,170500,95200\n' > Documents/budget-2026.csv
printf 'Meeting notes - 3 Nov\n\nShip the release on Thursday.\nWrite the changelog.\nRecord the demo video.\n' > Documents/notes.txt
for f in invoice-0412 invoice-0413 contract-draft resume; do printf '%%PDF-1.4\n%% made-up\n' > "Documents/$f.pdf"; done
mkdir -p Documents/Taxes Documents/Recipes
printf 'Pancakes: 2 eggs, 250 ml milk, 125 g flour\n' > Documents/Recipes/pancakes.txt

# Projects: this repository (a shallow clone) with uncommitted changes and a commit not pushed yet
git clone -q --depth 30 "file://$repo" Projects/file-explorer
(
    cd Projects/file-explorer
    git remote set-url origin https://github.com/AlexMedela/file-explorer.git
    git config user.name demo; git config user.email demo@example.com
    printf '\n// faster thumbnails\n' >> src/preview.c
    git commit -qam "Faster thumbnails"
    printf '\n// sidebar polish\n' >> src/render.c
    printf '\n## Unreleased\n- Demo video\n' >> CHANGELOG.md
    printf 'todo: record the demo\n' > NOTES.md
)
mkdir -p Projects/website/css Projects/website/img
printf '<!doctype html>\n<title>Home</title>\n' > Projects/website/index.html
printf '<!doctype html>\n<title>About</title>\n' > Projects/website/about.html
printf 'body { font: 16px system-ui; }\n' > Projects/website/css/site.css
cp Pictures/Wallpapers/dunes.jpg Projects/website/img/hero.jpg
cp Pictures/Wallpapers/tide.jpg Projects/website/img/team.jpg
# Backup: an older copy of the website (compare and sync)
mkdir -p Backup
cp -a Projects/website Backup/website
rm Backup/website/about.html Backup/website/img/team.jpg
printf 'body { font: 15px sans-serif; }\n' > Backup/website/css/site.css
printf '<!doctype html>\n<title>Old contact page</title>\n' > Backup/website/contact.html
touch -d '2026-09-01 10:00' Backup/website/css/site.css Backup/website/contact.html Backup/website/index.html
touch -d '2026-11-10 18:00' Projects/website/css/site.css

# Downloads: archives
(cd Projects && zip -qr ../Downloads/website-export.zip website)
tar -czf Downloads/photos-2026.tar.gz -C Pictures Wallpapers
printf 'made-up installer\n' > Downloads/setup-tool.AppImage

# Videos / Music: real blocks so the disk usage map has something to show
python3 - <<'PY'
import os
def blob(path, mb):
    with open(path, "wb") as f:
        chunk = os.urandom(1 << 20)
        for _ in range(mb): f.write(chunk)
for name, mb in [("Videos/holiday-2026.mkv", 220), ("Videos/talk-recording.mp4", 140), ("Videos/screencast.webm", 60),
                 ("Music/album-01.flac", 45), ("Music/album-02.flac", 38), ("Music/podcast.mp3", 18),
                 ("Downloads/ubuntu-26.04.iso", 160), ("Backup/system-backup.tar.zst", 120)]:
    blob(name, mb)
PY

# Datasets: a huge folder
mkdir -p Datasets/sensor-logs
python3 - Datasets/sensor-logs 200000 <<'PY'
import os, sys, time
d, n = sys.argv[1], int(sys.argv[2])
kinds = [("temp", "csv"), ("humidity", "csv"), ("pressure", "json"), ("error", "log"), ("wind", "csv")]
t0 = int(time.time()) - 200000 * 13 - 86400  # the last month, 13 s apart
for i in range(n):
    k, e = kinds[i % len(kinds)]
    p = f"{d}/{k}-{i:06d}.{e}"
    fd = os.open(p, os.O_CREAT | os.O_WRONLY, 0o644)
    os.ftruncate(fd, 200 + (i * 7919) % 3800)  # sparse: real sizes in the list, no space taken (disk usage map)
    os.close(fd)
    os.utime(p, (t0 + i * 13, t0 + i * 13))
PY

# Dates spread over the last weeks instead of all "today" (the repository keeps its own)
python3 - <<'PY'
import os, time
t0, k = int(time.time()) - 3600, 0
for root, dirs, files in os.walk("."):
    if root.startswith(("./Projects/file-explorer", "./Datasets", "./Projects/website", "./Backup/website")): continue
    for f in files + dirs:
        if f in ("file-explorer", "sensor-logs", "website") or (root == "." and f.startswith(".")): continue
        k += 1
        t = t0 - (k * 104729) % (40 * 86400)
        os.utime(os.path.join(root, f), (t, t), follow_symlinks=False)
PY

# Dotfiles: a plain prompt for the terminal panel
cat > .bashrc <<'EOF'
PS1='\[\e[38;5;111m\]\w\[\e[0m\] \[\e[38;5;245m\]$\[\e[0m\] '
EOF
echo "demo home ready: $H"
