<div align="center">

<img src="packaging/dev.fileexplorer.svg" width="84" alt="File Explorer logo">

# File Explorer

**Just another file explorer.** Fast, keyboard-first, for Linux.

[![CI](https://img.shields.io/github/actions/workflow/status/AlexMedela/file-explorer/ci.yml?branch=main&style=flat&label=CI&logo=githubactions&logoColor=white)](https://github.com/AlexMedela/file-explorer/actions)
[![Release](https://img.shields.io/github/v/release/AlexMedela/file-explorer?style=flat&color=7aa2f7&logo=github)](https://github.com/AlexMedela/file-explorer/releases/latest)
[![License: MIT](https://img.shields.io/badge/license-MIT-9ece6a?style=flat)](LICENSE)
![Linux](https://img.shields.io/badge/Linux-Wayland%20%7C%20X11-bb9af7?style=flat&logo=linux&logoColor=white)
![C11 + SDL3](https://img.shields.io/badge/C11-SDL3-e0af68?style=flat&logo=c&logoColor=white)
<img src="docs/img/pills/made-with-ai.svg" alt="Made with AI" height="20">

<img src="docs/img/hero.png" alt="File Explorer: a Git repository and an image folder side by side" width="900">

<img src="docs/img/pills/instant-on-huge-folders.svg" alt="Instant on huge folders">
<img src="docs/img/pills/keyboard-first.svg" alt="Keyboard-first">
<img src="docs/img/pills/split-panels-tabs.svg" alt="Split panels + tabs">
<img src="docs/img/pills/terminal-tabs.svg" alt="Terminal tabs">
<img src="docs/img/pills/disk-usage-map.svg" alt="Disk usage map">
<img src="docs/img/pills/git-status.svg" alt="Git status">
<img src="docs/img/pills/compare-sync.svg" alt="Compare + sync">
<img src="docs/img/pills/iphone-android.svg" alt="iPhone + Android">
<img src="docs/img/pills/archives-as-folders.svg" alt="Archives as folders">
<img src="docs/img/pills/snapshots.svg" alt="Snapshots">

</div>

## Why
- **Never waits on the disk.** A folder with 200,000 files is listed, with sizes and dates, in under 0.1 s. Listing
  runs on worker threads, so the GPU-drawn UI never stalls.
- **Your keyboard is enough.** `Ctrl+P` jumps anywhere, typing finds a file, and every shortcut can be rebound.
- **Everything in one window.** Split panels with their own tabs, terminal tabs, previews, search inside files,
  a disk usage map, folder compare, and archives you browse like folders.
- **Looks the same on every desktop.** GNOME, KDE, Xfce, Hyprland, Sway... with themes, icon sets and its own title bar.

## Install
| Distro | How | |
|---|---|---|
| Debian 13, Ubuntu 25.10+, Mint, Pop!_OS | add the repo once, then `sudo apt install file-explorer` ([commands](docs/install.md#debian-13-ubuntu-2510--2604--2610-mint-pop_os)) | ![ready](https://img.shields.io/badge/ready-apt%20repo-9ece6a?style=flat-square) |
| Fedora 43 / 44 | add the repo once, then `sudo dnf install file-explorer` ([commands](docs/install.md#fedora-43--44)) | ![ready](https://img.shields.io/badge/ready-rpm%20repo-9ece6a?style=flat-square) |
| openSUSE Tumbleweed / Leap 16 | add the repo once, then `sudo zypper install file-explorer` ([commands](docs/install.md#opensuse-tumbleweed--leap-160)) | ![ready](https://img.shields.io/badge/ready-rpm%20repo-9ece6a?style=flat-square) |
| Arch, CachyOS, Manjaro, EndeavourOS | `makepkg -si` in `packaging/arch` of a clone ([commands](docs/install.md#arch-cachyos-manjaro-endeavouros)) | ![ready](https://img.shields.io/badge/ready-PKGBUILD-9ece6a?style=flat-square) |
| Any distro (Ubuntu 22.04+, Debian 12+...) | AppImage from [Releases](https://github.com/AlexMedela/file-explorer/releases/latest) ([how](docs/install.md#any-other-distro-appimage)) | ![ready](https://img.shields.io/badge/ready-AppImage-9ece6a?style=flat-square) |

All packages, x86_64 and ARM64, update with your system. Details and other distros: [docs/install.md](docs/install.md).

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
[Install](docs/install.md) · [Keys](docs/keys.md) · [Settings](docs/settings.md) · [Integration](docs/integration.md) · [Building](docs/building.md) · [Architecture](docs/architecture.md)

## Licence
Made with AI: File Explorer is written with AI coding assistants, directed, tested and used daily by its author.

[MIT](LICENSE). Bundled icon sets and libraries keep their own licences, listed in [Building](docs/building.md#tools-and-self-tests).
