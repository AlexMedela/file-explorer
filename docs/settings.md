# Settings

[README](../README.md) · [Install](install.md) · [Keys](keys.md) · [Settings](settings.md) · [Integration](integration.md) · [Building](building.md) · [Architecture](architecture.md)

**Updates and several windows.** Installing or updating never touches your settings. Each window (and the
background instance) saves only what it changed itself, so two windows don't undo each other's changes. A window
still running the old version after an update restarts as the new one the next time you open something in it
(unless a transfer or a terminal tab is running). `settings_version` lets a later version adjust a stored default;
the previous file is kept as `settings.conf.bak` when that happens.

`~/.config/file-explorer/settings.conf` (plain `key=value`, saved automatically; "Open settings file" in the
palette). Remembers zoom, hidden files, folder sizes, search scope, sort order, preview panel, window size,
pinned folders, columns, and your tabs/split panes (`restore_session=0` to start fresh). `transfers_at_once=1`
(0 = all at once) and `verify_copies=0` are the transfer queue's settings. `renderer=auto` uses
OpenGL (steadier, and SDL's Vulkan renderer drops frames while the window is resized; Vulkan can take less memory
on Mesa drivers); `vulkan`/`opengl`/`software` force a backend.
`unified_surfaces=1` (Options -> Appearance -> Unified surfaces, off by default) gives the sidebar, the header (tab
strip and toolbar), the preview panel and the status bar the file area's background; the lines between them stay.
`show_separators=0` (Options -> Appearance -> Separator lines, on by default) leaves out the lines between the sidebar,
header, column headings, preview, terminal panel and status bar; an edge you can drag still lights up on hover.
`sidebar_brightness`, `header_brightness`, `main_brightness` (the file area), `preview_brightness` and
`status_brightness` (Options -> Appearance -> Surfaces, 0 by default) make that part darker (down to -30) or lighter
(up to 30, in percent) than the theme's own colour.
`tab_style=pills` (Options -> Appearance -> Tab style, `cards` by default) draws the tabs as rounded pills centred in a
box on the tab strip, instead of the open tab as a card joining the folder below it.
`title_bar=auto` (Options -> Appearance -> Title bar) draws Explorer's own title bar on every desktop, tiling
compositors included, so it looks the same everywhere: minimise / maximise / close at the end of the tab strip; drag
the empty strip or the logo to move, double-click it to maximise, right-click for the window menu, the edges resize.
`system` leaves the title bar to the desktop (on a tiling compositor: no buttons at all).
`FE_TITLE_BAR=app|system` overrides it for one run.
`window_buttons=0` (Options -> Appearance -> Window buttons, on by default) leaves out minimise / maximise / close, for
desktops that close and tile windows from the keyboard; the tab strip then runs to the window's edge.
`brand_name=app|host|custom|hidden` (Options -> Appearance -> Name above the sidebar) shows Explorer, this computer's
name, your own `brand_text=` (typed under it in Options), or nothing: the sidebar then starts at the top.
`brand_logo=0` (Options -> Appearance -> Logo) shows that name without the logo.
`round_corners=1` (Options -> Appearance -> Rounded corners, off by default) rounds the window's corners with Explorer's own title bar,
and draws a drop shadow round it the way GTK apps do (square and without shadow when maximised or snapped). On X11 this
needs a compositor (else the corners stay square) and the OpenGL renderer (Automatic picks it there); the shadow needs a
window manager that reads `_GTK_FRAME_EXTENTS` (mutter, KWin, xfwm4 and others). Turning it on takes a restart. `FE_SQUARE_CORNERS=1` / `FE_NO_SHADOW=1` turn the corners / the shadow off for one run.

**Dropping files** (Options > Behaviour > Mouse): a drop moves (copies onto another drive or from another app) unless
Ctrl / Shift / Ctrl+Shift say copy / move / link. `drop_ask=1` ("Ask what to do when dropping files") opens a menu
after each drop instead: Move here, Copy here, Link here (a symbolic link to each item), Cancel. Holding a modifier
still decides at once.

**Updates** (Options > Updates) shows the version and how this copy gets new ones. The AppImage asks GitHub for the
newest release once a day (`check_updates=0` turns it off; nothing but that request is sent) and shows a card once per
new version: "Update now" downloads it next to the AppImage, checks it against the release's SHA-256 and replaces the
file, then "Restart now" switches to it (tabs come back; or it simply starts next time). `update_auto_install=1`
downloads by itself, so the card only asks to restart. The AppImage's folder must be writable. Packages (apt, dnf,
zypper) update with the system and makepkg / source builds are told the command to run; "Check now" works for all.

Per-folder views live in `~/.config/file-explorer/folders.conf`, saved layouts in `layouts.conf`.

Theme > **System** (`theme=System`) is Light or Dark as the desktop prefers (the settings portal's dark / light
preference, else GTK's), with the desktop's accent colour where it has one (an accent picked in Options wins), and
follows changes while the app runs. On KDE it takes KDE's colour scheme itself (view, window, text and selection
colours from its settings portal, else `~/.config/kdeglobals` when the session is KDE).

`scrollbar_color=#RRGGBB` (Options -> Appearance -> Scrollbar colour: type `#RRGGBB` or `#RGB`, then Apply or Enter)
colours the scrollbars; `auto` (the field left empty) keeps the theme's own, which under Theme > System take the
desktop's accent colour.

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

Terminal panel: F4 shows a shell under the folder, one per tab, that follows the folder both ways while it waits at
its prompt (Ctrl+Shift+F4 moves the keyboard between the two). bash, zsh and fish move quietly: the panel's shell loads
a few lines of shell integration after your own startup files (bash: `--rcfile` that runs `~/.bashrc` first; zsh:
`ZDOTDIR` pointing at files that run yours; fish: `-C`), kept in `$XDG_RUNTIME_DIR/file-explorer/shell`, so the prompt
just updates and what you typed stays. Other shells, or one whose startup files fail before that, get a `cd` typed. A
`fastfetch` your startup files or fish's greeting run is skipped in the panel (typed at its prompt, it runs). Tabs that had it open get it back after a restart, with
a new shell; `terminal_panel_height` is its height. Terminal tabs: Shift+F4 opens a terminal tab in the current
folder (an SSH session in the same remote folder for SFTP connections); right-click for a local or an external terminal. The external
terminal is `$TERMINAL`, else xdg-terminal-exec, else the desktop's own; Options > Integration > Terminal app picks one of
the terminals installed instead (`external_terminal=`; listed are the usual ones on `$PATH` and those of terminal apps'
`.desktop` entries), and the menu then says "Open in" its name. Terminal tabs run your shell and look like your default
terminal (xdg-terminal-exec's choice, else $TERMINAL): font, colours, padding, cursor and scrollback are read from
foot, kitty, Alacritty or Ghostty config (the status bar names it: "colours and font from foot") and followed live (Options > Appearance > Terminal appearance); a font with
programming ligatures (Fira Code, JetBrains Mono...) shows them, kept to the cell grid. Ctrl+Shift+C/V
copy and paste, Shift+PageUp/PageDown scroll back, Ctrl+Shift+W closes, Ctrl+Tab switches tabs. Built on libvterm.

File icons > **System** (`icon_theme=System`) uses the desktop's icon theme for files, folders and the Trash: the one
the settings portal names (else KDE's or GTK's settings), followed when it changes. The bundled icon shows until the
desktop's is there, and wherever its theme has none.

Custom file icons: `~/.config/file-explorer/icons/<Name>/` (Options -> Appearance -> "Icon themes folder"). SVGs
named like the built-in icons replace them (`python.svg`, `folder.svg`, `folder-src.svg`...), `ext-<extension>.svg`
sets the icon for any extension, and `theme.conf` (`base=Catppuccin`) picks the set the missing icons come from.
`trash-full.svg` is the Trash's bin when something is in it (`trash.svg` when empty; a theme with only `trash.svg`
uses it for both). `emblem-symbolic-link.svg` and `emblem-git.svg` replace the marks on links and Git
repositories: drawn on the icon's lower right at about a third of its size (with both, the Git one sits above);
without them the usual marks stay. `net-<name>.svg` adds an icon for network locations (offered whichever theme is selected). The folder's README.txt
lists every icon name.
