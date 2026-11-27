# Settings

[README](../README.md) · [Install](install.md) · [Keys](keys.md) · [Settings](settings.md) · [Integration](integration.md) · [Building](building.md) · [Architecture](architecture.md)

**Updates and several windows.** Installing or updating never touches your settings. Each window (and the
background instance) saves only what it changed itself, so two windows don't undo each other's changes. A window
still running the old version after an update restarts as the new one the next time you open something in it
(unless a transfer or a terminal tab is running). `settings_version` lets a later version adjust a stored default;
the previous file is kept as `settings.conf.bak` when that happens.

`~/.config/file-explorer/settings.conf` (plain `key=value`, saved automatically; "Open settings file" in the
palette). Remembers zoom, hidden files, folder sizes, search scope, sort order
(`folders_first=0` mixes folders and files), preview panel, window size,
`compress_format=zip` and `compress_level=normal` (the Compress sheet's last choice: `zip`, `tar.gz`, `tar.xz`,
`tar.zst`, `7z`; `fast`, `normal`, `best` or a level number),
pinned folders, columns, and your tabs/split panes (`restore_session=0` to start fresh). `transfers_at_once=1`
(0 = all at once) and `verify_copies=0` are the transfer queue's settings. `renderer=auto` uses
OpenGL (steadier, and SDL's Vulkan renderer drops frames while the window is resized; Vulkan can take less memory
on Mesa drivers; Options -> Advanced shows the one Automatic picked, e.g. "Automatic (OpenGL)"); `vulkan`/`opengl`/`software`
force a backend.
`unified_surfaces=1` (Options -> Appearance -> Unified surfaces, off by default) gives the sidebar, the header (tab
strip and toolbar), the preview panel and the status bar the file area's background; the lines between them stay.
`show_separators=0` (Options -> Appearance -> Separator lines, on by default) leaves out the lines between the sidebar,
header, column headings, preview, terminal panel and status bar; an edge you can drag still lights up on hover.
`navigation_selection_opacity` (Options -> Appearance -> Navigation selection opacity, 40 by default) sets the accent
mix, from 0 to 100 in steps of 5, for selected sidebar rows, selected Options categories and the active pill tab.
`navigation_selector_mark=1` (Options -> Appearance -> Toggle navigation selector mark, On by default) shows the small
accent edge on selected sidebar and Options navigation rows; off keeps the selection fill without that mark. The file
list's own selection colours and hover tints are unchanged.
`sidebar_brightness`, `header_brightness`, `main_brightness` (the file area), `preview_brightness` and
`status_brightness` (Options -> Appearance -> Surfaces, 0 by default) make that part darker (down to -30) or lighter
(up to 30, in percent) than the theme's own colour.
`pathbar_brightness` and `searchbar_brightness` (0 by default, -30..30) move only the path field/pill enclosure and
the folder search field respectively, independently of the header strip. `transparent_pathbar=1` (off by default)
removes only the path field's fill, leaving a faint outline and the accent focus outline while editing; pills and the
search field stay filled. `path_bar_text=1` (Options -> Appearance -> Path as text, off by default) shows the path as
plain text instead of clickable parts (its start cut off when it doesn't fit); a click anywhere on it edits it.
`ribbon=1` (Options -> Appearance -> Ribbon, off by default) adds a row of icons for common file actions under each
panel's navigation bar (cut, copy, paste, rename, move to Trash, compress to, new folder / file, open,
open with, terminal, properties, select all, views, group by, hidden files, details panel; Extract here / Mount when
they apply), with the right-click menu's icons and their names in a tooltip. Compress to, Open with, Views and Group by
open the right-click menu's submenu as a dropdown. They act on that panel; greyed when they don't apply.
`ribbon_position=below` (`above` the path bar, the list's `bottom`, `left`, `right`; also by dragging the dots at its
end - a bar of the accent shows where it goes) and `ribbon_overflow=more` (what doesn't fit: a "..." menu; `wrap`
onto more rows / columns; `scroll`: arrows at both ends and the wheel slide it along). These keys were added after `settings_version` and default to the theme colour on older
config files: no migration runs, so existing appearances are unchanged.
`tab_style=pills` (Options -> Appearance -> Tab style, `cards` by default) draws the tabs as rounded pills centred in a
box on the tab strip, instead of the open tab as a card joining the folder below it.
`tab_style=cards|pills` (Options -> Appearance -> Tab style, `cards` by default) picks one of two tab modes: Classic tabs
(`cards`) put the open tab as a card joining the folder below it, above the navigation bar; Pills (`pills`) draw rounded
tabs centred in a box on a strip below the navigation bar. The placement is part of the mode. The pills row uses the
file-area background and a higher-contrast enclosure, with extra padding and a slightly taller centred row.
`hide_single_tab=1` (Options -> Behaviour -> Hide tab bar with one tab, off by default) hides the tab band while only one
tab is open, giving that row back to the folder; open another tab and the band returns, and new tabs still fit.

`title_bar=default` (the default, Options -> Appearance -> Title bar) draws Explorer's own title bar on every desktop,
tiling compositors included, so it looks the same everywhere: minimise / maximise / close at the end of the tab strip;
drag the empty strip or the logo to move, double-click it to maximise, right-click for the window menu, the edges resize.
`title_bar=system` leaves the title bar to the desktop: its own decorations and window controls (on a tiling compositor:
no buttons at all). Settings from older versions (`title_bar=auto` or `title_bar=app`) load as `default`.
`FE_TITLE_BAR=default|system` (the older `app` value still works) overrides it for one run.
`window_buttons=0` (Options -> Appearance -> Window buttons, on by default) leaves out minimise / maximise / close, for
desktops that close and tile windows from the keyboard; the tab strip then runs to the window's edge.
`brand_name=app|host|custom|hidden` (Options -> Appearance -> Name above the sidebar) shows Explorer, this computer's
name, your own `brand_text=` (typed under it in Options), or nothing: the sidebar then starts at the top.
`brand_logo=0` (Options -> Appearance -> Logo) shows that name without the logo.
`repo_tag=full|icon|hidden` (Options -> Appearance -> Repository tag) styles the tag on a repository folder: its
branch, its mark alone, or none (the name then takes the room). `repo_icon=keep|match` (Options -> Appearance ->
Repository folder icon) lets a repository folder keep its own icon or wear the one that matches it (the Git folder,
the archive of a Borg backup).
`round_corners=1` (Options -> Appearance -> Rounded corners, off by default) rounds the window's corners with Explorer's own title bar,
and draws a drop shadow round it the way GTK apps do (square and without shadow when maximised or snapped). On X11 this
needs a compositor (else the corners stay square) and the OpenGL renderer (Automatic picks it there); the shadow needs a
window manager that reads `_GTK_FRAME_EXTENTS` (mutter, KWin, xfwm4 and others). Turning it on takes a restart. `FE_SQUARE_CORNERS=1` / `FE_NO_SHADOW=1` turn the corners / the shadow off for one run.

**Dropping files** (Options > Behaviour > Mouse): a drop moves (copies onto another drive or from another app) unless
Ctrl / Shift / Ctrl+Shift say copy / move / link. `drop_ask=1` ("Ask what to do when dropping files") opens a menu
after each drop instead: Move here, Copy here, Link here (a symbolic link to each item), Cancel. Holding a modifier
still decides at once.

**Programs and scripts** (Options > Behaviour > Programs): opening an executable file (an AppImage or other program, or a
script: starting with `#!`, or a `.sh` / `.bash` file without it, run by the shell) asks first: Run (in the background, no window: for apps), Run in terminal (the external terminal, as for "Open in external terminal" - Options > Integration >
Terminal app; it waits for Enter when the program ends, so its output can be read),
Open (scripts: as a document, in your editor) or Cancel. "Don't ask again" keeps that answer; `exec_open=ask|run|terminal|open`
("Opening programs and scripts") changes it back. Programs started this way start in their own folder. A program without
the executable bit (an AppImage just downloaded) always asks "Make executable and run" first; a script without it opens as
a document. Right-click > Run / Run in terminal runs a program or script without the question. Files in network folders
are never run.

**Context menus** (Options > Context menus) hides entries of the file and folder right-click menu (Open in new
tab / split, Open with, Disk usage, Delete permanently, Compress to, the terminal entries, Views / Sort by / Group by,
Properties, ...); `menu_hidden=` lists the hidden ones (e.g. `menu_hidden=open_split_down,disk_usage`). The ribbon,
sidebar, drive and terminal-panel menus and the keyboard shortcuts are unchanged.

**Updates** (Options > Updates) shows the version and how this copy gets new ones. The AppImage asks GitHub for the
newest release once a day (`check_updates=0` turns it off; nothing but that request is sent) and shows a card once per
new version: "Update now" downloads it next to the AppImage, checks it against the release's SHA-256 and replaces the
file, then "Restart now" switches to it (tabs come back; or it simply starts next time). `update_auto_install=1`
downloads by itself, so the card only asks to restart. The AppImage's folder must be writable. Only the AppImage
does this: installed through a package manager (apt, dnf, zypper, Portage...) it never asks GitHub for anything and the
page only shows the version, and makepkg / source builds are told the command to run.

Per-folder views live in `~/.config/file-explorer/folders.conf`, saved layouts in `layouts.conf`, the file chooser's last
Save folder per file type in `picker.conf`.

Theme > **System** (`theme=System`) is Light or Dark as the desktop prefers (the settings portal's dark / light
preference, else GTK's), with the desktop's accent colour where it has one (an accent picked in Options wins), and
follows changes while the app runs. On KDE it takes KDE's colour scheme itself (view, window, text and selection
colours from its settings portal, else `~/.config/kdeglobals` when the session is KDE).
Which themes it switches between is up to you: **Light theme** and **Dark theme** (shown under Theme while System is
picked; `theme_light=` / `theme_dark=`, any built-in or JSON theme) default to Light and Dark. KDE's colours are used
only while the one for the current mode is left on its default.
`system_accent=1` (Options -> Appearance -> Desktop's accent colour, off by default) uses the desktop's accent with
every theme, where the desktop has one; otherwise the accent picked above.

`scrollbar_color=#RRGGBB` (Options -> Appearance -> Scrollbar colour: type `#RRGGBB` or `#RGB`, then Apply or Enter)
colours the scrollbars; `auto` (the field left empty) keeps the theme's own, which under Theme > System take the
desktop's accent colour.

`smooth_scroll=1` (Options -> Appearance -> Smooth scrolling, on by default): the wheel eases every list, sheet and
menu to its new place, and a quick run of notches one way scrolls further each time (up to three times as far). A
touchpad moves the view as the fingers do, and a swipe that lets go at speed carries on, slowing down; a click, a
key or the wheel stops it. Off: the wheel jumps a fixed step.

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
the terminals installed instead, or type an executable name or absolute path (no arguments) in Terminal executable and
press Apply; Default clears it back to Automatic (`external_terminal=`; the list holds the usual ones on `$PATH` and
those of terminal apps' `.desktop` entries). Either way the menu says "Open in" its name. Terminal tabs run your shell and look like your default
terminal (xdg-terminal-exec's choice, else $TERMINAL): font, colours, padding, cursor and scrollback are read from
foot, kitty, Alacritty or Ghostty config (the status bar names it: "colours and font from foot") and followed live (Options > Appearance > Terminal appearance); a font with
programming ligatures (Fira Code, JetBrains Mono...) shows them, kept to the cell grid. Ctrl+Shift+C/V
copy and paste, Shift+PageUp/PageDown scroll back, Ctrl+Shift+W closes, Ctrl+Tab switches tabs. Built on libvterm.

File icons > **System** (`icon_theme=System`) uses the desktop's icon theme for files, folders and the Trash: the one
the settings portal names (else KDE's or GTK's settings), followed when it changes. The bundled icon shows until the
desktop's is there, and wherever its theme has none.

`app_icon_mode=custom|file_icon|default` (Options -> Appearance -> App icon, directly under File icons) selects
the window and launcher icon. `app_icon` (empty by default) remembers the absolute `.svg`/`.png` path typed in that
row. The row previews the icon in effect with no background chip, and its active button is primary. **Apply** uses the
typed path (SVG keeps its own colours) and selects Custom; **File-Icon** uses the folder icon of the currently selected
File icons set and follows changes to that setting live; **Default** clears `app_icon`, shows the `Default` placeholder
in the field, and restores the accent-coloured built-in logo. File-Icon also installs that folder icon as the launcher
override (`~/.local/share/icons/hicolor/256x256/apps/dev.fileexplorer.png`); Default removes it and restores the recoloured built-in artwork. Older settings with a non-empty
`app_icon` are treated as Custom, an empty one as Default, and a legacy `app_icon_mode=system` as File-Icon.

`title_icon` (Options -> Appearance -> Title bar icon, directly below App icon, empty by default) is an
absolute `.svg` or `.png` path that overrides the icon at the left of the title bar. Its row previews the icon in effect
without a background chip and has **Apply** and **Default** buttons; Default clears the path and restores the small
built-in logo, while the typed field keeps its path until you press Default. This override follows the **Logo** toggle
below it: with Logo off, no icon is drawn in that slot.

Custom file icons: `~/.config/file-explorer/icons/<Name>/` (Options -> Appearance -> "Icon themes folder"). SVGs
named like the built-in icons replace them (`python.svg`, `folder.svg`, `folder-src.svg`...), `ext-<extension>.svg`
sets the icon for any extension, and `theme.conf` (`base=Catppuccin`) picks the set the missing icons come from.
`trash-full.svg` is the Trash's bin when something is in it (`trash.svg` when empty; a theme with only `trash.svg`
uses it for both). `emblem-symbolic-link.svg` and `emblem-git.svg` replace the marks on links and Git
repositories, and `emblem-symbolic-share.svg` adds the mark on shared folders: drawn on the icon's lower right at
about a third of its size (with more than one, the next one sits above); without them the desktop icon theme's own
artwork is used, and the app's own when the theme has none either. `net-<name>.svg` adds an icon for network locations (offered whichever theme is selected). The folder's README.txt
lists every icon name.
