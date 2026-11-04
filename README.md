<div align="center">

<img src="packaging/dev.fileexplorer.svg" width="84" alt="File Explorer logo">

# File Explorer

**Just another file explorer.** Fast, keyboard-first, for Linux.

[![CI](https://img.shields.io/github/actions/workflow/status/AlexMedela/file-explorer/ci.yml?branch=main&style=flat&label=CI&logo=githubactions&logoColor=white)](https://github.com/AlexMedela/file-explorer/actions)
[![Release](https://img.shields.io/github/v/release/AlexMedela/file-explorer?style=flat&color=7aa2f7&logo=github)](https://github.com/AlexMedela/file-explorer/releases/latest)
[![License: MIT](https://img.shields.io/badge/license-MIT-9ece6a?style=flat)](LICENSE)
![Linux](https://img.shields.io/badge/Linux-Wayland%20%7C%20X11-bb9af7?style=flat&logo=linux&logoColor=white)
![C11 + SDL3](https://img.shields.io/badge/C11-SDL3-e0af68?style=flat&logo=c&logoColor=white)
![Made with Claude](https://img.shields.io/badge/made_with-Claude-D97757?style=flat&logo=claude&logoColor=white)

<img src="docs/img/hero.png" alt="File Explorer: a Git repository and an image folder side by side" width="900">

</div>

## Features
- **Instant on huge folders**: 200,000 files listed with sizes and dates in under 0.1 s; the UI never waits on the disk.
- **Keyboard-first**: `Ctrl+P` jumps to any folder or file, typing finds one, and every shortcut can be rebound.
- **Split panels and tabs**: split any way you like, each panel with its own tabs; save layouts and switch between them.
- **Terminal tabs**: a built-in terminal next to your files, opened in the current folder with `F4`.
- **Search**: filter as you type, search subfolders, or search inside files (ripgrep when installed).
- **Disk usage map**: see what takes the space as a treemap or sunburst, and clean it up right there.
- **Git status**: changed, new and ignored files marked in repositories, with the branch in the path bar.
- **Compare and sync**: compare two folders side by side and sync them one way or both.
- **Phones and cameras**: iPhones and Android phones on a cable, no GVfs needed.
- **Archives as folders**: browse zip, tar, 7z, rar and iso like folders; extract or compress from the menu.
- **Snapshots**: previous versions of files from Snapper, Timeshift or ZFS, browse and restore.
- **Drives and network**: mount USB drives, SMB/SFTP shares and cloud storage (rclone) from the sidebar.
- **Looks the same everywhere**: GPU-drawn with its own title bar on GNOME, KDE, Xfce, Hyprland, Sway; themes and icon sets.

## Install
| Distro | How | |
|---|---|---|
| Debian 13, Ubuntu 25.10+, Mint, Pop!_OS | add the repo once, then `sudo apt install file-explorer` ([commands](docs/install.md#debian-13-ubuntu-2510--2604--2610-mint-pop_os)) | ![ready](https://img.shields.io/badge/ready-apt%20repo-9ece6a?style=flat) |
| Fedora 43 / 44 | add the repo once, then `sudo dnf install file-explorer` ([commands](docs/install.md#fedora-43--44)) | ![ready](https://img.shields.io/badge/ready-rpm%20repo-9ece6a?style=flat) |
| openSUSE Tumbleweed / Leap 16 | add the repo once, then `sudo zypper install file-explorer` ([commands](docs/install.md#opensuse-tumbleweed--leap-160)) | ![ready](https://img.shields.io/badge/ready-rpm%20repo-9ece6a?style=flat) |
| Arch, CachyOS, Manjaro, EndeavourOS | `makepkg -si` in `packaging/arch` of a clone ([commands](docs/install.md#arch-cachyos-manjaro-endeavouros)) | ![ready](https://img.shields.io/badge/ready-PKGBUILD-9ece6a?style=flat) |
| Any distro (Ubuntu 22.04+, Debian 12+...) | AppImage from [Releases](https://github.com/AlexMedela/file-explorer/releases/latest) ([how](docs/install.md#any-other-distro-appimage)) | ![ready](https://img.shields.io/badge/ready-AppImage-9ece6a?style=flat) |

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
[MIT](LICENSE). Bundled icon sets and libraries keep their own licences, listed in [Building](docs/building.md#tools-and-self-tests).
