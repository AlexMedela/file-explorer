<div align="center">

<img src="packaging/dev.fileexplorer.svg" width="84" alt="File Explorer logo">

# File Explorer

**Just another file explorer.** Fast, keyboard-first, for Linux.

[![CI](https://img.shields.io/github/actions/workflow/status/AlexMedela/file-explorer/ci.yml?branch=main&style=flat-square&label=CI)](https://github.com/AlexMedela/file-explorer/actions)
[![License: MIT](https://img.shields.io/badge/license-MIT-9ece6a?style=flat-square)](LICENSE)
![Linux](https://img.shields.io/badge/Linux-Wayland%20%7C%20X11-bb9af7?style=flat-square)
![C11 + SDL3](https://img.shields.io/badge/C11-SDL3-e0af68?style=flat-square)

<img src="docs/img/hero.png" alt="File Explorer: a Git repository and an image folder side by side" width="900">

![Instant on huge folders](https://img.shields.io/badge/%E2%9A%A1-instant%20on%20huge%20folders-2b2d3a?style=flat-square)
![Keyboard-first](https://img.shields.io/badge/%E2%8C%A8-keyboard--first-2b2d3a?style=flat-square)
![Split panels and tabs](https://img.shields.io/badge/%E2%96%A6-split%20panels%20%2B%20tabs-2b2d3a?style=flat-square)
![Terminal tabs](https://img.shields.io/badge/%3E__-terminal%20tabs-2b2d3a?style=flat-square)
![Disk usage map](https://img.shields.io/badge/%E2%97%95-disk%20usage%20map-2b2d3a?style=flat-square)
![Git status](https://img.shields.io/badge/%E2%8E%87-git%20status-2b2d3a?style=flat-square)
![Folder compare and sync](https://img.shields.io/badge/%E2%87%86-compare%20%2B%20sync-2b2d3a?style=flat-square)
![Phones without GVfs](https://img.shields.io/badge/%F0%9F%93%B1-iPhone%20%2B%20Android-2b2d3a?style=flat-square)
![Archives as folders](https://img.shields.io/badge/%F0%9F%97%84-archives%20as%20folders-2b2d3a?style=flat-square)
![Snapshots](https://img.shields.io/badge/%E2%9F%B2-snapshots-2b2d3a?style=flat-square)

</div>

## Why
- **Never waits on the disk.** A folder with 200,000 files is listed, with sizes and dates, in under 0.1 s. Listing
  runs on worker threads, so the GPU-drawn UI never stalls.
- **Your keyboard is enough.** `Ctrl+P` jumps anywhere, typing finds a file, and every shortcut can be rebound.
- **Everything in one window.** Split panels with their own tabs, terminal tabs, previews, search inside files,
  a disk usage map, folder compare, and archives you browse like folders.
- **Looks the same on every desktop.** GNOME, KDE, Xfce, Hyprland, Sway... with themes, icon sets and its own title bar.

## Install
| Distro | Command | |
|---|---|---|
| Arch, CachyOS, Manjaro, EndeavourOS | `makepkg -si` in `packaging/arch` of a clone | ![ready](https://img.shields.io/badge/ready-PKGBUILD-9ece6a?style=flat-square) |
| Debian 13, Ubuntu 24.04+, Mint, Pop!_OS | `sudo apt install file-explorer` | ![soon](https://img.shields.io/badge/soon-apt%20repo-lightgrey?style=flat-square) |
| Fedora, openSUSE | `sudo dnf install file-explorer` | ![soon](https://img.shields.io/badge/soon-rpm%20repo-lightgrey?style=flat-square) |
| Any distro | AppImage from [Releases](https://github.com/AlexMedela/file-explorer/releases) | ![soon](https://img.shields.io/badge/soon-AppImage-lightgrey?style=flat-square) |
| From source | see [Building](docs/building.md) | ![ready](https://img.shields.io/badge/ready-source-9ece6a?style=flat-square) |

On Arch: `git clone https://github.com/AlexMedela/file-explorer && cd file-explorer/packaging/arch && makepkg -si`.
An AUR package follows once the AUR reopens account registration (paused because of spam).

Installing never replaces your current file manager or file picker: switch those on in **Options > Integration**
(default file manager, "Show in folder", file chooser for other apps). See [Integration](docs/integration.md).

## First steps
| Key | |
|---|---|
| `Ctrl+P` | Go to any folder or file |
| Type a name | Jump to it, `Ctrl+F` to filter or search below |
| `Ctrl+\` | Split the panel (each has its own tabs) |
| `Ctrl+T` / `Ctrl+W` | New tab / close tab |
| `Space` | Preview panel |
| `F4` | Terminal tab in this folder |
| `Ctrl+Shift+U` | Disk usage map |
| `Ctrl+,` | Options: themes, keys, views, title bar |

All keys: [docs/keys.md](docs/keys.md)

## Docs
[Keys](docs/keys.md) · [Settings](docs/settings.md) · [Integration](docs/integration.md) · [Building](docs/building.md) · [Architecture](docs/architecture.md)

## Licence
[MIT](LICENSE). Bundled icon sets and libraries keep their own licences, listed in [Building](docs/building.md#tools-and-self-tests).
