#!/usr/bin/env python3
"""Download the icon sets used by tools/gen_icons.py (MIT licensed) into third_party/.
Material is already vendored; this fetches Catppuccin (mocha) and Symbols for our icon names."""
import os, sys, urllib.request, json
root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from icon_aliases import CATPPUCCIN, SYMBOLS, FOLDERS, catppuccin_folder, symbols_folder, canonical_names

def fetch(url, dst):
    if os.path.exists(dst): return True
    try:
        with urllib.request.urlopen(url, timeout=20) as r: data = r.read()
    except Exception:
        return False
    open(dst, "wb").write(data)
    return True

sets = [
    ("catppuccin", "https://raw.githubusercontent.com/catppuccin/vscode-icons/main/icons/mocha/{}.svg",
     "https://raw.githubusercontent.com/catppuccin/vscode-icons/main/LICENSE", CATPPUCCIN, "_file"),
    ("symbols", "https://raw.githubusercontent.com/miguelsolorio/vscode-symbols/main/src/icons/files/{}.svg",
     "https://raw.githubusercontent.com/miguelsolorio/vscode-symbols/main/LICENSE", SYMBOLS, "document"),
]
canon = canonical_names(root)
for name, url, lic, aliases, default in sets:
    d = os.path.join(root, "third_party", "icons-" + name)
    os.makedirs(d, exist_ok=True)
    fetch(lic, os.path.join(d, "LICENSE"))
    want = {default} | {aliases.get(c, c) for c in canon}
    ok = sum(fetch(url.format(w), os.path.join(d, w + ".svg")) for w in sorted(want))
    print(f"{name}: {ok}/{len(want)} icons")

# Folder icons: Material (vendored next to the file icons; "folder" itself is generated upstream from
# folder-base's outline in the default #90a4ae), Catppuccin folder_*, Symbols src/icons/folders/.
MAT = "https://raw.githubusercontent.com/material-extensions/vscode-material-icon-theme/main/icons/{}.svg"
mat_dir = os.path.join(root, "third_party", "material-icons")
for n in ["folder-base"] + list(FOLDERS):
    fetch(MAT.format(n), os.path.join(mat_dir, n + ".svg"))
base = os.path.join(mat_dir, "folder-base.svg")
if os.path.exists(base):
    import re
    svg = open(base).read()
    path = re.search(r'<path id="folder"[^>]*?d="([^"]+)"', svg).group(1)
    open(os.path.join(mat_dir, "folder.svg"), "w").write(
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16"><path fill="#90a4ae" d="{path}"/></svg>')
    os.remove(base)
cat = os.path.join(root, "third_party", "icons-catppuccin")
sym = os.path.join(root, "third_party", "icons-symbols")
for n in ["folder"] + list(FOLDERS):
    fetch(sets[0][1].format(catppuccin_folder(n)), os.path.join(cat, catppuccin_folder(n) + ".svg"))
    fetch("https://raw.githubusercontent.com/miguelsolorio/vscode-symbols/main/src/icons/folders/{}.svg".format(symbols_folder(n)),
          os.path.join(sym, symbols_folder(n) + ".svg"))
print("folders:", len(FOLDERS) + 1)
