#!/usr/bin/env python3
"""Generate src/net_icons.h: the icons offered for network locations (Connect to server > Icon).
Generic shapes are drawn here; brand logos come from third_party/net-icons/ (Simple Icons, CC0; Material Icon
Theme, MIT). #FEFEFE marks the theme's text colour (monochrome logos that would vanish on one of the themes)."""
import os, re
root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
SI = os.path.join(root, "third_party", "net-icons", "simple-icons")
MAT = os.path.join(root, "third_party", "net-icons", "material")

def svg24(body): return '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24">' + body + '</svg>'
CLOUD = "M6.5 19a4.5 4.5 0 0 1-.6-8.96A6 6 0 0 1 17.6 8.6 5.2 5.2 0 0 1 17.5 19Z"
GENERIC = {
    "network": svg24('<g fill="none" stroke="#7FB2FF" stroke-width="1.8"><circle cx="12" cy="12" r="9"/>'
                     '<ellipse cx="12" cy="12" rx="4" ry="9"/><path d="M3 12h18M4.8 7.5h14.4M4.8 16.5h14.4"/></g>'),
    "server": svg24('<rect x="3" y="3.5" width="18" height="7.5" rx="2" fill="#90A4AE"/><rect x="3" y="13" width="18" height="7.5" rx="2" fill="#90A4AE"/>'
                    '<circle cx="7" cy="7.25" r="1.3" fill="#66BB6A"/><circle cx="7" cy="16.75" r="1.3" fill="#66BB6A"/>'
                    '<rect x="11" y="6.5" width="7" height="1.5" rx=".75" fill="#455A64"/><rect x="11" y="16" width="7" height="1.5" rx=".75" fill="#455A64"/>'),
    "vm": svg24('<rect x="2" y="3.5" width="20" height="14" rx="2" fill="#9575CD"/><rect x="4" y="5.5" width="16" height="10" rx="1" fill="#311B92"/>'
                '<path d="M7 8.2l2.6 2.3L7 12.8" fill="none" stroke="#FFFFFF" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"/>'
                '<rect x="11" y="12" width="5" height="1.5" rx=".75" fill="#FFFFFF"/><rect x="8" y="18.5" width="8" height="2" rx="1" fill="#9575CD"/>'),
    "nas": svg24('<rect x="5" y="2.5" width="14" height="19" rx="2.2" fill="#78909C"/><rect x="7.5" y="5" width="9" height="3" rx="1" fill="#37474F"/>'
                 '<rect x="7.5" y="9.5" width="9" height="3" rx="1" fill="#37474F"/><circle cx="9" cy="17.5" r="1.3" fill="#66BB6A"/>'
                 '<circle cx="12.5" cy="17.5" r="1.3" fill="#B0BEC5"/>'),
    "disk": svg24('<rect x="2.5" y="6" width="19" height="12" rx="2.5" fill="#90A4AE"/><rect x="5.5" y="11.25" width="9" height="1.5" rx=".75" fill="#455A64"/>'
                  '<circle cx="17.5" cy="12" r="1.4" fill="#66BB6A"/>'),
    "tunnel": svg24('<path d="M2 20.5V11a10 10 0 0 1 20 0v9.5Z" fill="#4DB6AC"/><path d="M6 20.5V12a6 6 0 0 1 12 0v8.5Z" fill="#00695C"/>'
                    '<path d="M12 13.6v1.8M12 17.6v1.8" stroke="#E0F2F1" stroke-width="1.5" stroke-linecap="round"/>'),
    "cloud": svg24('<path d="' + CLOUD + '" fill="#64B5F6"/>'),
    "home": svg24('<path d="M12 3.2 2.8 11h2.7v9h5v-5.5h3V20h5v-9h2.7Z" fill="#81C784"/>'),
    "aws": svg24('<path d="' + CLOUD + '" fill="#FF9900"/><path d="M7 15.2q5 2.6 10 0" fill="none" stroke="#232F3E" stroke-width="1.6" stroke-linecap="round"/>'
                 '<path d="M15.4 14.2l1.8.9-.6 1.9" fill="none" stroke="#232F3E" stroke-width="1.4" stroke-linecap="round" stroke-linejoin="round"/>'),
    "windows": svg24('<path d="M3 5.2 10.6 4v7.4H3ZM11.8 3.8 21 2.5v8.9h-9.2ZM3 12.6h7.6V20L3 18.8ZM11.8 12.6H21v8.9l-9.2-1.3Z" fill="#0078D4"/>'),
    "gdrive": svg24('<path d="M8.6 3h6.8l6.6 11.4h-6.8Z" fill="#FBBC04"/><path d="M8.6 3 2 14.4l3.4 5.9L12 8.9Z" fill="#34A853"/>'
                    '<path d="M8.8 14.4H22l-3.4 5.9H5.4Z" fill="#4285F4"/>'),
    "onedrive": svg24('<path d="' + CLOUD + '" fill="#0078D4"/><path d="M9.6 19a3.6 3.6 0 0 1-.4-7.17A4.8 4.8 0 0 1 18 11.2a3.9 3.9 0 0 1-.5 7.8Z" fill="#28A8EA"/>'),
    "dropbox": svg24('<path d="M7 3.5 2 6.75 7 10l5-3.25ZM17 3.5l-5 3.25L17 10l5-3.25ZM2 13.25 7 16.5l5-3.25L7 10ZM17 10l-5 3.25 5 3.25 5-3.25ZM7 17.6l5 3.25 5-3.25-5-3.25Z" fill="#0061FF"/>'),
    "oracle": svg24('<rect x="2.5" y="7" width="19" height="10" rx="5" fill="none" stroke="#F80000" stroke-width="2.6"/>'),
}
BRAND = {"akamai": "0096D6", "apple": "MARK", "archlinux": "1793D1", "cloudflare": "F38020", "debian": "D70A53",
         "digitalocean": "0080FF", "fedora": "51A2DA", "freebsd": "AB2B28", "hetzner": "D50C2D", "linux": "FCC624",
         "nixos": "5277C3", "ovh": "MARK", "proxmox": "E57000", "qemu": "FF6600", "raspberrypi": "C51A4A",
         "redhat": "EE0000", "truenas": "0095D5", "ubuntu": "E95420", "virtualbox": "2F61B4",
         "vultr": "007BFC"}
GROUPS = [
    ("General", [("network", "Network"), ("tunnel", "Tunnel"), ("server", "Server"), ("vm", "Virtual machine"), ("nas", "NAS"), ("disk", "Disk"),
                 ("cloud", "Cloud"), ("home", "Home"), ("console", "Terminal"), ("database", "Database"),
                 ("docker", "Docker"), ("kubernetes", "Kubernetes")]),
    ("Cloud & hosting", [("aws", "AWS"), ("azure", "Azure"), ("gcp", "Google Cloud"), ("digitalocean", "DigitalOcean"),
                         ("hetzner", "Hetzner"), ("akamai", "Akamai / Linode"), ("vultr", "Vultr"), ("oracle", "Oracle Cloud"),
                         ("ovh", "OVHcloud"), ("cloudflare", "Cloudflare"),
                         ("gdrive", "Google Drive"), ("onedrive", "OneDrive"), ("dropbox", "Dropbox")]),
    ("Systems", [("linux", "Linux"), ("ubuntu", "Ubuntu"), ("debian", "Debian"), ("archlinux", "Arch Linux"),
                 ("fedora", "Fedora"), ("redhat", "Red Hat"), ("nixos", "NixOS"), ("freebsd", "FreeBSD"),
                 ("windows", "Windows"), ("apple", "macOS"), ("raspberrypi", "Raspberry Pi"), ("proxmox", "Proxmox"),
                 ("truenas", "TrueNAS"),
                 ("virtualbox", "VirtualBox"), ("qemu", "QEMU")]),
]

def load(key):
    if key in GENERIC: return GENERIC[key]
    p = os.path.join(MAT, key + ".svg")
    if os.path.exists(p): return open(p).read().strip()
    s = open(os.path.join(SI, key + ".svg")).read().strip()
    s = re.sub(r"<title>.*?</title>", "", s)
    col = "#FEFEFE" if BRAND[key] == "MARK" else "#" + BRAND[key]
    return s.replace("<path ", '<path fill="%s" ' % col)

def cstr(s):
    s = s.replace("\\", "\\\\").replace('"', '\\"')
    assert all(ord(c) < 128 for c in s)
    return '"' + s + '"'

out = ["// Generated by tools/gen_net_icons.py - do not edit.", "// Icons for network locations: generic shapes, Simple Icons (CC0) and Material Icon Theme (MIT) logos.",
       "typedef struct { const char *key, *label; int group; const char *svg; } NetIconDef;",
       "static const char *const g_net_icon_groups[] = {" + ", ".join(cstr(g) for g, _ in GROUPS) + "};",
       "static const NetIconDef g_net_icons[] = {"]
for gi, (_, items) in enumerate(GROUPS):
    for key, label in items:
        out.append("    {%s, %s, %d, %s}," % (cstr(key), cstr(label), gi, cstr(load(key))))
out += ["};", "#define NET_ICON_BUILTIN ((int)(sizeof g_net_icons / sizeof g_net_icons[0]))", ""]
open(os.path.join(root, "src", "net_icons.h"), "w").write("\n".join(out))
print(sum(len(i) for _, i in GROUPS), "network icons")
