# Installing

[README](../README.md) · [Install](install.md) · [Keys](keys.md) · [Settings](settings.md) · [Integration](integration.md) · [Building](building.md) · [Architecture](architecture.md)

Packages are built on the [openSUSE Build Service](https://build.opensuse.org/package/show/home:AlexMedela/file-explorer)
for x86_64 and ARM64. Add the repository once; updates then come with your system's normal updates.
The same instructions, per distro, are also on [software.opensuse.org](https://software.opensuse.org/download.html?project=home%3AAlexMedela&package=file-explorer).

## Debian 13, Ubuntu 25.10 / 26.04 / 26.10, Mint, Pop!_OS
Pick the line for your release (`Debian_13`, `xUbuntu_25.10`, `xUbuntu_26.04`, `xUbuntu_26.10`):
```sh
R=Debian_13
echo "deb https://download.opensuse.org/repositories/home:/AlexMedela/$R/ /" | sudo tee /etc/apt/sources.list.d/file-explorer.list
curl -fsSL "https://download.opensuse.org/repositories/home:AlexMedela/$R/Release.key" | gpg --dearmor | sudo tee /etc/apt/trusted.gpg.d/file-explorer.gpg >/dev/null
sudo apt update && sudo apt install file-explorer
```
(Mint 23 / Pop!_OS 26.04: use `xUbuntu_26.04`.)

## Fedora 43 / 44
```sh
sudo dnf config-manager addrepo --from-repofile="https://download.opensuse.org/repositories/home:AlexMedela/Fedora_$(rpm -E %fedora)/home:AlexMedela.repo"
sudo dnf install file-explorer
```

## openSUSE Tumbleweed / Leap 16.0
```sh
sudo zypper addrepo https://download.opensuse.org/repositories/home:AlexMedela/openSUSE_Tumbleweed/home:AlexMedela.repo   # Leap: .../16.0/...
sudo zypper refresh && sudo zypper install file-explorer
```

## Arch, CachyOS, Manjaro, EndeavourOS
```sh
git clone https://github.com/AlexMedela/file-explorer && cd file-explorer/packaging/arch && makepkg -si
```
An AUR package follows once the AUR reopens account registration (paused because of spam).

## Any other distro: AppImage
Ubuntu 22.04 / 24.04 LTS, Debian 12 and anything newer (x86_64 and ARM64): download
`file-explorer-<version>-<arch>.AppImage` from [Releases](https://github.com/AlexMedela/file-explorer/releases), then
```sh
chmod +x file-explorer-*.AppImage && ./file-explorer-*.AppImage
```
It brings its own SDL 3 and libraries, and updates itself: once a day it looks for a new version and offers it in a
card (Options > Updates to turn that off, or to let it download by itself); AppImageUpdate / Gear Lever work too. For the menu entry, the phone udev rule and the
administrator prompt's own polkit message, install a package instead, or integrate it with Gear Lever / AppImageLauncher.

## Removing
`sudo apt remove file-explorer` / `sudo dnf remove file-explorer` / `sudo zypper remove file-explorer`, then delete the
repository file you added. Your settings in `~/.config/file-explorer` stay unless you delete them.
