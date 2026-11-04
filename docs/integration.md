# Integration with the desktop

[README](../README.md) · [Keys](keys.md) · [Settings](settings.md) · [Integration](integration.md) · [Building](building.md) · [Architecture](architecture.md)

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
