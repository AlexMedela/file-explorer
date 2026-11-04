# Settings

[README](../README.md) · [Keys](keys.md) · [Settings](settings.md) · [Integration](integration.md) · [Building](building.md) · [Architecture](architecture.md)

**Updates and several windows.** Installing or updating never touches your settings. Each window (and the
background instance) saves only what it changed itself, so two windows don't undo each other's changes. A window
still running the old version after an update restarts as the new one the next time you open something in it
(unless a transfer or a terminal tab is running). `settings_version` lets a later version adjust a stored default;
the previous file is kept as `settings.conf.bak` when that happens.

`~/.config/file-explorer/settings.conf` (plain `key=value`, saved automatically; "Open settings file" in the
palette). Remembers zoom, hidden files, folder sizes, search scope, sort order, preview panel, window size,
pinned folders, columns, and your tabs/split panes (`restore_session=0` to start fresh). `transfers_at_once=1`
(0 = all at once) and `verify_copies=0` are the transfer queue's settings. `renderer=auto` prefers
Vulkan (lower memory than GL on Mesa); `opengl`/`software` force a backend.
`title_bar=auto` (Options -> Appearance -> Title bar) draws Explorer's own title bar on every desktop, tiling
compositors included, so it looks the same everywhere: minimise / maximise / close at the end of the tab strip; drag
the empty strip or the logo to move, double-click it to maximise, right-click for the window menu, the edges resize.
`system` leaves the title bar to the desktop (on a tiling compositor: no buttons at all).
`FE_TITLE_BAR=app|system` overrides it for one run.

Per-folder views live in `~/.config/file-explorer/folders.conf`, saved layouts in `layouts.conf`.

Colour schemes: `~/.config/file-explorer/themes/*.json` (Options → "Create theme from current colours" writes a
starting point). Any key you leave out comes from the `base` theme:
```json
{ "name": "My theme", "base": "dark", "colors": { "bg": "#1b1b20", "accent": "#ff8800", "folder": "#e9b44c" } }
```
The app icon follows the accent colour: in the window, and for launchers and docks through a recoloured copy at
`~/.local/share/icons/hicolor/scalable/apps/dev.fileexplorer.svg` (none for the default Blue; delete it after
uninstalling). Docks and launchers may only pick up a new colour once they reload their icons.

Changed shortcuts are stored as `key=<command>=<keys>|<keys>` lines (only the ones that differ from the defaults).

Remote servers: **Connect to server** (palette, or type `sftp://` / `smb://` ... in the path bar) mounts SFTP, SMB,
FTP/FTPS, WebDAV(S) and NFS through GVfs. Passwords are passed to GVfs only when it asks (SSH key
passphrases too) and are not stored unless you tick "Remember password", which keeps them in the system keyring
(Secret Service: gnome-keyring, KWallet, KeePassXC) - never in the app's files. Questions such as an unknown SSH host
key are shown to you. Saved servers appear in the sidebar's Network section: a click connects (asking for a password
only if the server wants one), right-click to edit or forget them (forgetting also removes the stored password). Protocols whose GVfs backend isn't installed are marked as unavailable.
Phones and cameras plugged in with a cable show under Devices (dim until connected; click to connect, ⏏ when done).
The app talks to them itself - no usbmuxd, libimobiledevice or GVfs needed: iPhones and iPads (usbmux, lockdown,
AFC; the first time, unlock the iPhone and tap Trust - the pairing is kept in `~/.local/share/file-explorer/lockdown`,
or the system's from usbmuxd is used), with their photos and videos (DCIM...) and an "App Documents" folder holding
the files of apps that share them (VLC, Documents...); Android phones over MTP (unlock the phone and pick "File
transfer" in its USB notification); cameras over PTP. Each one is served as a folder under
`$XDG_RUNTIME_DIR/file-explorer/devices/<name>` by a small helper (`file-explorer --device-mount`, FUSE) that keeps
running until it's disconnected or unplugged, so other programs can use the folder too. If a device drops off USB, tabs
in it leave its folder; when it's back within a minute it's connected again by itself and the tab returns.
`FE_DEVICE_DEBUG=1 file-explorer --device-mount <usb id> DIR` runs the helper by hand and prints what happens. Folder listings ask the
device for a whole folder at once (MTP) or over several connections (iPhone). They copy, rename and delete like any
folder, but they have no trash (deleting asks first) and keep no permissions; MTP can't change a file in place, so a
changed file is sent again whole. Opening a device needs USB access for your user (the udev rule
`70-file-explorer-phones.rules`, installed by `install.sh`). A device the app can't have (another program is using it)
is opened through GVfs instead when GVfs knows it; GVfs also handles anything else it lists. While the app has an
iPhone, usbmuxd (and the tools using it) can't reach it until it's plugged in again. `FE_NO_OWN_PHONES=1` leaves
everything to GVfs.

Each saved server can have a display name and a display icon (generic server/VM/NAS/cloud shapes, cloud providers
such as AWS, Azure, Google Cloud, Hetzner, and operating systems), plus your own: `net-<name>.svg` in any folder of
the icon themes folder.

The system file chooser, default file manager and phone access are in [Integration](integration.md).

Cloud (sidebar → Cloud, shown when [rclone](https://rclone.org) is installed): every remote `rclone listremotes`
reports - Google Drive, OneDrive, Dropbox, S3, WebDAV... - is a place. The + runs `rclone config` in a terminal tab to
add one; the list follows rclone's configuration by itself. A click connects and opens it: `rclone mount` runs as a
systemd user service (`~/.config/systemd/user/file-explorer-cloud-*.service`, `--vfs-cache-mode full`) mounted at
`~/.local/share/file-explorer/cloud/<remote>`, so it stays mounted when the app is closed and, with "Connect at login"
(right-click), comes up without the app. The eject button disconnects; the dot shows connecting / retrying. When a
sign-in has expired the app offers `rclone config reconnect` in a terminal. A remote you mounted yourself shows up
here as connected, at its own folder. Needs FUSE (`fuse3`).

Options → Behaviour has "Start Explorer at login" (XDG autostart, starts in the background) and "Keep running in
the tray when closed" (needs a tray in your panel).

Terminal tabs: F4 opens a terminal tab in the current folder (an SSH session in the same remote folder for SFTP
connections); right-click for a local or an external terminal. It runs your shell and looks like your default
terminal (xdg-terminal-exec's choice, else $TERMINAL): font, colours, padding, cursor and scrollback are read from
foot, kitty, Alacritty or Ghostty config and followed live (Options > Appearance > Terminal appearance). Ctrl+Shift+C/V
copy and paste, Shift+PageUp/PageDown scroll back, Ctrl+Shift+W closes, Ctrl+Tab switches tabs. Built on libvterm.

Custom file icons: `~/.config/file-explorer/icons/<Name>/` (Options -> Appearance -> "Icon themes folder"). SVGs
named like the built-in icons replace them (`python.svg`, `folder.svg`, `folder-src.svg`...), `ext-<extension>.svg`
sets the icon for any extension, and `theme.conf` (`base=Catppuccin`) picks the set the missing icons come from.
`net-<name>.svg` adds an icon for network locations (offered whichever theme is selected). The folder's README.txt
lists every icon name.
