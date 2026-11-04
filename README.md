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

![Instant on huge folders](https://img.shields.io/badge/Instant_on_huge_folders-bf8700?style=flat)
![Keyboard-first](https://img.shields.io/badge/Keyboard--first-0969da?style=flat)
![Split panels + tabs](https://img.shields.io/badge/Split_panels_%2B_tabs-8250df?style=flat)
![Terminal tabs](https://img.shields.io/badge/Terminal_tabs-1a7f37?style=flat&logo=gnometerminal&logoColor=white)
![Disk usage map](https://img.shields.io/badge/Disk_usage_map-bc4c00?style=flat)
![Git status](https://img.shields.io/badge/Git_status-cf222e?style=flat&logo=git&logoColor=white)
![Compare + sync](https://img.shields.io/badge/Compare_%2B_sync-0598bc?style=flat)
![iPhone + Android](https://img.shields.io/badge/iPhone_%2B_Android-1b7c83?style=flat&logo=android&logoColor=white)
![Archives as folders](https://img.shields.io/badge/Archives_as_folders-0550ae?style=flat)
![Snapshots](https://img.shields.io/badge/Snapshots-a475f9?style=flat)

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
Made with Claude: directed, tested and used daily by its author.

[MIT](LICENSE). Bundled icon sets and libraries keep their own licences, listed in [Building](docs/building.md#tools-and-self-tests).
