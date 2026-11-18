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
In Save the name field has the keyboard (Tab switches to the list); Ctrl+L / F6 still edits the path, Ctrl+F searches,
and Alt+←/→/↑, Ctrl+H and F5 work without losing the typed name. Selecting a file puts its name in the field.
Save starts in the folder you last saved that type of file into (by extension: the next PDF where the last PDF went).
Its sidebar has the Cloud section too: open a file straight from a cloud remote or save one into it. The remote stays
mounted after the picker closes so the app can read or write the path it got; files pass through rclone's cache
(`~/.cache/rclone/vfs`) and are dropped from it an hour after their last use (saved ones once uploaded), nothing is
left in your folders.

## Phones and cameras
iPhones / iPads and Android phones / cameras on a cable use the built-in connector (no GVfs needed). The package
installs a udev rule (`70-file-explorer-phones.rules`) that gives the user at the seat access to them; built from
source, `install.sh` puts it in place. GVfs (`gvfs-mtp`, `gvfs-gphoto2`) is used as a fallback when installed.

## Borg backups
Folders that are [BorgBackup](https://www.borgbackup.org/) repositories get a **Borg backup** (or **Borg 2 backup**)
pill, also with Git status off. Opening one shows its backups, one dated folder each, read-only: it runs `borg mount`
into `$XDG_RUNTIME_DIR/fe-borg/`. That needs `borg` and borg's FUSE support for Python: `python-pyfuse3` (Arch),
`python3-pyfuse3` (Debian / Ubuntu / Fedora) or llfuse. Borg 2 repositories need Borg 2 (still in beta), found as `borg2`
or as a `borg` that says it's 2.x. An encrypted repository asks for its passphrase (it can be kept in the system
keyring), keyfile repositories use your keys as usual, and `BORG_PASSPHRASE` / `BORG_PASSCOMMAND` from your session are
honoured. Inside a backup the bar has **Compare with now** and **Restore…** (back to where it lives on this
computer), copying or dragging out works as anywhere. Open backups show under **Backups** in the sidebar;
**Close backups** (or Eject there) unmounts them. Unused ones are unmounted by themselves - Borg 1 after 20 seconds,
because borg locks a mounted Borg 1 repository and backups to it would fail meanwhile, Borg 2 after two minutes - and
all of them when the app quits. "Open as folder" (or a new tab) shows the repository's own files.

**Add to sidebar** (right-click the repository, or inside its backups) keeps it under Backups: a click opens it,
right-click > **Edit…** sets its name and icon, and for Borg 2 **Keep mounted while the app runs** mounts it in the
background (its passphrase from the keyring; Borg 2 doesn't lock it, so backups go on). Saved backups are stored in
the settings file as `borg=` lines. A Borg 1 repository in an SFTP folder is read by borg on the server (`ssh://`, over the
connection the folder uses, or your ssh keys / agent); without borg there it is read through the folder, slower.

## Administrator actions
"Retry as administrator" runs a small helper through `pkexec`, so it needs polkit and an authentication agent.
Full desktops start one; on tiling compositors start e.g. `hyprpolkitagent` or `polkit-gnome`.
