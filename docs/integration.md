# Integration with the desktop

[README](../README.md) · [Install](install.md) · [Keys](keys.md) · [Settings](settings.md) · [Integration](integration.md) · [Building](building.md) · [Architecture](architecture.md)

Installing File Explorer never takes over a role from your current file manager, and packages never touch your
settings or your default apps: no install script runs `xdg-mime`, edits portal configs or writes to `~/.config`.
Each of these is your choice.

## Options > Integration
Three switches, each showing who has the role now. Turning one off puts back what was there before.

| Switch | What it does |
|---|---|
| **Default file manager** | Sets `inode/directory` in your `~/.config/mimeapps.list` (and in the desktop's own `<desktop>-mimeapps.list` when that one names folders), remembering the previous app to restore later. |
| **"Show in folder" opens here** | Answers `org.freedesktop.FileManager1` on the session bus (ShowItems / ShowFolders: browsers' downloads, "Open containing folder" in many apps) while File Explorer runs, and writes a user D-Bus activation file so it can start on request. Another file manager that is running keeps the name. |
| **File chooser for other apps** | With [xdg-desktop-portal-termfilechooser](https://github.com/hunkyburrito/xdg-desktop-portal-termfilechooser) installed: writes a small wrapper, termfilechooser's config and `~/.config/xdg-desktop-portal/<desktop>-portals.conf` (`FileChooser=termfilechooser`), then restarts the portal. Files that were there are kept as `*.fe-backup`. |

On the first start, File Explorer asks once whether it should be the default file manager.

The same by hand:
```sh
xdg-mime default dev.fileexplorer.desktop inode/directory     # default file manager
```

## The picker window
`file-explorer --pick file|files|dir|save --out FILE [PATH]` opens a picker window (app id `dev.fileexplorer.picker`)
with an Open / Save / Choose bar and writes the chosen path(s) to FILE, one per line (nothing when cancelled). The
"File chooser for other apps" switch connects it to xdg-desktop-portal-termfilechooser, so browsers, Electron and GTK
apps (`GTK_USE_PORTAL=1`) get it as their Open / Save dialog. The picker never changes your settings.
Its Network sidebar section includes cloud remotes: open a file straight from a remote or save one into it. The remote stays
mounted after the picker closes so the app can read or write the path it got; files pass through rclone's cache
(`~/.cache/rclone/vfs`) and are dropped from it an hour after their last use (saved ones once uploaded), nothing is
left in your folders.

## Phones and cameras
iPhones / iPads and Android phones / cameras on a cable use the built-in connector (no GVfs needed). The package
installs a udev rule (`70-file-explorer-phones.rules`) that gives the user at the seat access to them; built from
source, `install.sh` puts it in place. GVfs (`gvfs-mtp`, `gvfs-gphoto2`) is used as a fallback when installed.

## Administrator actions
"Retry as administrator" runs a small helper through `pkexec`, so it needs polkit and an authentication agent.
Full desktops start one; on tiling compositors start e.g. `hyprpolkitagent` or `polkit-gnome`.

### Local shared folders

Configured local share roots keep their base folder icon with protocol-specific badges in folder lists, grids,
path headers, sidebar bookmarks, and previews. SMB uses `folder-remote-smb`,
NFS uses `folder-remote-nfs`, and WebDAV/other shares use `folder-publicshare`,
as small overlays at the lower right. Custom themes can supply SVGs with these names (see
[Settings](settings.md)). Children keep their normal icons
unless they are also shared explicitly. Detection reads these local files without
running commands or requesting administrator access, and caches results for ten
seconds:

- SMB: `/etc/samba/smb.conf` (including absolute `include` files) and
  `/var/lib/samba/usershares`, plus a configured `usershare path`.
- NFS: `/etc/exports`, `/etc/exports.d/*.exports`, and `/var/lib/nfs/etab`
  when readable.
- WebDAV: explicit Apache `<Directory /absolute/path>` blocks containing
  `Dav On` in `/etc/httpd/conf/httpd.conf`, `/etc/httpd/conf.d/*.conf`,
  `/etc/apache2/sites-enabled/*.conf`, and `/etc/apache2/conf-enabled/*.conf`.

When running in Distrobox, detection also reads the host configuration under
`/run/host`, including host Samba include files.

These icons indicate configuration, not live server availability. Unreadable
files are skipped. Samba variable substitutions, registry-backed shares,
nonstandard include layouts, and WebDAV configurations for other servers are
not detected. Paths are matched directly; aliases through symbolic links are
not resolved. No remote network requests are made.

The parser regression checks run headlessly:

```sh
cc -std=gnu11 -Wno-unused-function tools/local-shares-selftest.c -o /tmp/fe-local-shares-test -pthread
/tmp/fe-local-shares-test
```

### SVG previews and host applications

SVG files render directly in grid thumbnails and the preview panel, without an
external thumbnailer. Rendering preserves transparency and aspect ratio, with an
8 MiB source limit and a bounded output size. The built-in renderer supports SVG
paths, shapes, and gradients; advanced features such as text, filters, embedded
images, and animation may not render fully. SVGZ still uses installed thumbnailers.

When running through Distrobox, Open with also scans native host applications and
host Flatpak exports under `/run/host`. Host-only handlers launch through
`distrobox-host-exec` or `host-spawn`; they are added only when a host launcher is
available. This allows SVG handlers such as Inkscape installed on the host to be
offered even when their executables are absent from the container.
