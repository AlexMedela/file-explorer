#!/bin/sh
# Create a test directory with N empty files (+ some folders). Usage: mkfiles.sh DIR N
set -e
dir="$1"; n="${2:-100000}"
mkdir -p "$dir"
cd "$dir"
python3 - "$n" <<'PY'
import os, sys
n = int(sys.argv[1])
exts = ["txt","png","mp4","c","pdf","zip","mp3","rs","json",""]
for i in range(n):
    e = exts[i % len(exts)]
    name = f"file_{i}" + (f".{e}" if e else "")
    os.close(os.open(name, os.O_CREAT | os.O_WRONLY, 0o644))
for i in range(min(200, n // 100 + 1)):
    os.makedirs(f"folder_{i}", exist_ok=True)
PY
echo "created $n files in $dir"
