# Building from source

[README](../README.md) · [Install](install.md) · [Keys](keys.md) · [Settings](settings.md) · [Integration](integration.md) · [Building](building.md) · [Architecture](architecture.md)

```sh
git clone https://github.com/AlexMedela/file-explorer && cd file-explorer
./build.sh package && sudo PREFIX=/usr/local ./install.sh
```
On Arch, `cd packaging/arch && makepkg -si` builds a package from the checkout instead.

## Build & run
```sh
./build.sh            # release build -> build/file-explorer
./build.sh debug      # ASan/UBSan build
./build/file-explorer [PATH|FILE|URI]... [--tab PATH] [--split PATH] [-n|--new-window]
```
Folders open as tabs; a file opens its folder with the file selected. If the app is already running, the
paths open as tabs in that window (Options → Behaviour → "Open in the running window"), unless `--new-window`.
Build dependencies (pkg-config names): `sdl3` (3.2 or newer) `freetype2 harfbuzz fontconfig libarchive vterm` (libvterm 0.3+)
`fuse3 openssl` (OpenSSL 3), plus gcc or clang. SDL 3.2 is the floor: Arch, Fedora 41+, Debian 13, Ubuntu 25.04+,
openSUSE Tumbleweed; not Ubuntu 24.04 / Debian 12.

At runtime:
- `libdecor` (+ GTK 3 for its nicer look): only for `title_bar=system` on GNOME Wayland, which draws no title bars
  itself (by default the app draws its own).
- A polkit authentication agent for "Retry as administrator" (desktops ship one; on tiling compositors start e.g.
  `hyprpolkitagent` or `polkit-gnome`).
- Optional: `xdg-utils` (files open with the default app from the desktop entries; xdg-open is the fallback), freedesktop
  thumbnailers (ffmpegthumbnailer, evince, glycin, ...) for video/PDF/HEIF/SVG previews, `udisks2` to mount drives,
  `gvfs` for network locations, `rclone` for cloud storage. Phones and cameras on a cable use the built-in connector
  (`gvfs-mtp` / `gvfs-gphoto2` are fallbacks).

`file-explorer --version` prints the version and the SDL it runs on (include it in bug reports).


`FE_DEBUG_DND=1` logs what dragging files out to other apps does (X11 / Wayland protocol steps) on stderr, for drags
that misbehave on some desktop.
## Tools and self-tests
- `./build/file-explorer --bench DIR [QUERY]` – headless listing benchmark; with QUERY also a recursive search (time, CPU, peak memory)
Self-tests and screenshots run headless (SDL's offscreen video driver + software renderer): no window appears,
nothing touches your settings or recent list, and a running instance is left alone. Set `SDL_VIDEODRIVER=wayland`
(or `x11`) to watch them in a real window.

- `./build/file-explorer --screenshot out.bmp DIR [--tab DIR]... [--split DIR]` – render one frame to an image
- `./build/file-explorer --selftest DIR` – drive tabs/split/selection/navigation through the real input
  handlers (use with `./build.sh debug`; DIR should have >1000 entries with folders first, e.g. from mkfiles.sh)
- `./build/file-explorer --selftest-ops` – file operations end-to-end in `/tmp/fe-ops-test` with a private
  trash: conflicts (each answer, together and one by one), the transfer queue (turns, order, pause), verify after
  copy (`FE_SHOTS=dir` also saves screenshots of menus/dialogs/progress/the queue)
- `./build/file-explorer --selftest-m4` – palette, recursive search, live refresh, folder sizes
- `./build/file-explorer --selftest-m6` – panels, tab drag & drop, layout save/restore
- `./build/file-explorer --selftest-m7` – icons, columns, views & thumbnails, grouping, folder views
- `./build/file-explorer --selftest-m8` – Open with, Properties, batch rename, type filter, trash, archives
- `./build/file-explorer --selftest-m9` – themes, Options, shortcuts, mouse options, layouts, Quick Access, tree view
- `FE_RECORD=/tmp/fe-rec ./build/file-explorer` – diagnostics: saves the frames shown right after each click (half
  size BMPs) plus a state log, to track down visual glitches
- `./build/file-explorer --selftest-grep` – search inside files: matcher, results + lines, options, folding, open at line, preview, result limit, ripgrep output (stand-in rg)
- `./build/file-explorer --selftest-du` – disk usage: sizes, mount rules, treemap + sunburst layout, picking, keys, trash / copy, rescans, menus
- `./build/file-explorer --selftest-cmp` – folder compare: what differs, marks per side, following folders, differences only, sync / mirror / both ways, starting from one panel or a tab
- `./build/file-explorer --selftest-devices` – phones and cameras without a device: plists (XML, binary), USB descriptors and a made-up bus, a made-up iPhone (usbmux, TCP, lockdown, pairing with certificate checks, AFC, installation_proxy, house_arrest) and a made-up MTP phone, each served through FUSE and used with plain file calls (headless)
- `./build/file-explorer --selftest-admin` – retry as administrator (copy, rename, new folder, trash -> delete) with a stand-in pkexec, previous versions from made-up Snapper / Timeshift / ZFS layouts: open, restore, compare, root-only snapshots; the Snapshots view with a stand-in snapper: list, what changed, browse + restore + compare, create / delete, "Allow me to browse snapshots"
- `./build/file-explorer --selftest-git` – Git status: parser, badges in every view, path bar chip + details, refresh rules, the features toggle, branch pills on repository folders, SFTP repositories over ssh (stand-in git and ssh)
- `./build/file-explorer --selftest-m11` – terminal tabs: config readers, output/colours, input, selection, scrollback
- `./build/file-explorer --selftest-m10` – single instance (real second launch), tabs popup, drives/network/phones with fake
  tools, unreadable folders, odd names, deep/long paths, fractional scaling
- `./build/file-explorer --selftest-m8` – open with, properties, batch rename, type filter, trash, archives
- `./build/file-explorer --selftest-m5` – preview panel, thumbnailers, zoom, pins, settings + session restore

Self-tests use private config/cache/state dirs under `/tmp`, never your real ones, and never launch
external programs (xdg-open, terminals, new windows).

Third-party: `third_party/stb_image.h` (public domain / MIT), `third_party/nanosvg*.h` (zlib),
file and folder icon themes (all MIT): [Material Icon Theme](https://github.com/material-extensions/vscode-material-icon-theme),
[Catppuccin](https://github.com/catppuccin/vscode-icons) (mocha) and [Symbols](https://github.com/miguelsolorio/vscode-symbols);
"Monokai" is the Symbols shapes recoloured with the Monokai palette. `tools/fetch_icons.py` downloads the sets,
`tools/gen_icons.py` regenerates `src/icons_data.h`. Pick one with the palette ("Icon theme: …") or `icon_theme=` in settings.
Network location icons: brand logos from [Simple Icons](https://simpleicons.org) (CC0) and Material Icon Theme (MIT),
in `third_party/net-icons/`; `tools/gen_net_icons.py` regenerates `src/net_icons.h`. Logos are trademarks of their owners.
- `tools/mkfiles.sh DIR N` – generate a test directory with N files
