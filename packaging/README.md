# Packaging

How File Explorer gets to each distro. To build it yourself from source, see [docs/building.md](../docs/building.md)
(dependencies, `./build.sh`); to install a package, see [docs/install.md](../docs/install.md).

| Folder | Distros | How it is built |
|---|---|---|
| `obs/` | Fedora, openSUSE, Debian, Ubuntu | On the [openSUSE Build Service](https://build.opensuse.org/package/show/home:AlexMedela/file-explorer) (OBS): one package, built for every distro release from the same files |
| `arch/` | Arch, CachyOS, Manjaro, EndeavourOS | Locally: `cd packaging/arch && makepkg -si` |
| `appimage/` | Any other distro (glibc 2.35+) | `packaging/appimage/build.sh` in an Ubuntu 22.04 container; the release workflow runs it for x86_64 and aarch64 |

The rest (`dev.fileexplorer.desktop`, `.policy`, `.svg`, `udev/`) is shared by every package; `install.sh` puts it in place.

## OBS (`obs/`)
- `_service` takes the source from GitHub and names it after the latest release tag (`v0.12.1` -> `0.12.1`).
- `file-explorer.spec` builds the RPMs (Fedora and openSUSE), `debian.*` + `file-explorer.dsc` the Debian / Ubuntu packages.
- A tagged release triggers the rebuild on OBS (`.github/workflows/release.yml`).

The files keep the same names and live in one folder because OBS reads them as a single package. Distro releases are
added on the OBS project (one repository per release, e.g. `Fedora_45`), not here.

To try a package build locally: `osc checkout home:AlexMedela/file-explorer`, copy the files from `obs/` over it,
then `osc build Fedora_45 x86_64 file-explorer.spec` (or `Debian_13 x86_64 file-explorer.dsc`).

## Versions
The only version to bump for a release is `APP_VERSION` in `src/app.c` (plus a `## <version>` section in
`CHANGELOG.md`). The release workflow refuses a tag that doesn't match it. The version numbers in `obs/` are
placeholders that OBS replaces with the tag's version, and the Arch package takes its version from git.
