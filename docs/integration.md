# Integration with the desktop

[README](../README.md) · [Keys](keys.md) · [Settings](settings.md) · [Integration](integration.md) · [Building](building.md) · [Architecture](architecture.md)

Installing File Explorer never takes over a role from your current file manager. Each of these is your choice.

## Default file manager
Folders opened from other apps (and `xdg-open ~/Downloads`) use the default handler for `inode/directory`:
```sh
xdg-mime default dev.fileexplorer.desktop inode/directory
```
To go back, set your previous one the same way (e.g. `org.gnome.Nautilus.desktop`, `org.kde.dolphin.desktop`).
"Show in folder" from browsers goes through the `org.freedesktop.FileManager1` D-Bus service, which File Explorer
doesn't provide yet.

## File chooser (Open / Save dialogs of other apps)
System file chooser: `file-explorer --pick file|files|dir|save --out FILE [PATH]` opens a picker window (app id
`dev.fileexplorer.picker`) with an Open / Save / Choose bar and writes the chosen path(s) to FILE. With
[xdg-desktop-portal-termfilechooser](https://github.com/hunkyburrito/xdg-desktop-portal-termfilechooser) and the
files in `packaging/portal/` (wrapper, config, `hyprland-portals.conf`), every app's Open/Save/Choose-folder dialog
becomes this file manager - browsers, Electron apps, GTK apps (`GTK_USE_PORTAL=1`). The picker never changes your settings.

## Phones and cameras
iPhones / iPads and Android phones / cameras on a cable use the built-in connector (no GVfs needed). The package
installs a udev rule (`70-file-explorer-phones.rules`) that gives the user at the seat access to them; built from
source, `install.sh` puts it in place. GVfs (`gvfs-mtp`, `gvfs-gphoto2`) is used as a fallback when installed.

## Administrator actions
"Retry as administrator" runs a small helper through `pkexec`, so it needs polkit and an authentication agent.
Full desktops start one; on tiling compositors start e.g. `hyprpolkitagent` or `polkit-gnome`.
