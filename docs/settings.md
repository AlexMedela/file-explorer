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
Options > Appearance > **Application icon** shows the current icon. Click **Choose image** to select an SVG
or PNG; the choice applies immediately. **Use system** clears the override and follows the desktop icon theme.
The saved `app_icon` value is the selected image path (empty for System). Missing images fall back to desktop artwork.

Taskbar icons come from the desktop launcher independently of the window icon. Explorer copies the original
artwork into the user's data directory and updates its per-user launcher to reference that copy. With Distrobox,
it preserves the exported launch command and uses the exported desktop file's ID for window grouping. Restart
once after installing this fix so an existing window picks up the corrected ID. No re-export is needed for each
icon change once the launcher exists.

Options > Appearance > **Title bar** has two choices:

- **Integrated header** (`title_bar=app`) places minimize/maximize/close on the first tab/navigation row.
  Tabs above navigation share the controls row; with tabs below or only one tab, navigation shares it.
  Drag the left label or empty header space to move, double-click it to maximize, or right-click for the window menu.
- **Desktop title bar** (`title_bar=system`) leaves decorations and buttons to the desktop. Tiling/window-manager
  setups that omit a native title bar are supported without reserving extra space inside the app.

**Show window controls** hides/shows the three app-drawn buttons in Integrated header mode. Hiding them gives their
space back to tabs/navigation and leaves header dragging, edge resizing, shortcuts and the window menu available.
Desktop-provided controls are configured by the window manager. App-drawn controls use the desktop icon theme's
window-action shapes when available, with built-in vector fallbacks, independently of the selected file-icon set.
Older `title_bar=auto` settings remain readable; the UI no longer exposes Automatic or "Explorer's own" choices.
`FE_TITLE_BAR=app|system` still overrides the mode for one run.

**Header label** chooses File-Explorer (default), Hostname, Custom or Hidden. **Custom header text** applies a custom
single-line label; Default restores File-Explorer without changing window-control visibility. Hostname uses the
machine/container hostname visible to the running app. Saved keys are `header_label=app|hostname|custom|hidden`,
`header_text=...`, and `show_window_controls=1|0`.

`round_corners=1` (Options > Appearance > Rounded corners, off by default) rounds the window's corners
and draws a drop shadow with the integrated header. It stays square without a shadow when maximized or snapped.
On X11 this needs a compositor and the OpenGL renderer; the shadow needs a window manager that reads
`_GTK_FRAME_EXTENTS`. Changing this setting takes a restart. `FE_SQUARE_CORNERS=1` and `FE_NO_SHADOW=1`
disable corners and the shadow for one run.

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

**System theme and icons.** Options > Appearance > Theme > **System** uses the desktop's colors where exposed.
KDE palette colors come from the Settings portal or shared `kdeglobals`; other desktops use their standard
light/dark preference with an accent tint when full colors are unavailable. **Follow system appearance** automatically
selects **System** in the Theme dropdown. Turning it off restores the previous theme; choosing a manual theme also
turns following off. Older `match_system_background=1` settings are kept compatible and select System on startup.
All surface brightness controls still apply afterward.

File icons > **System** follows the current desktop icon theme; **System: Reversal** (or another installed name)
selects that theme explicitly. Themes are discovered from `~/.local/share/icons`, `~/.icons`, and the XDG data
locations. Distrobox also checks `/run/host/usr/share/icons` when available. PNG/SVG files, sizes/scales and theme
inheritance are supported; missing icons use hicolor, then bundled icons. XPM is unsupported. Install Reversal in the
container or the shared `~/.local/share/icons` if it is not visible. Restart or open "Icon themes folder" to rescan.
System icon choices take precedence over `FE_NERD_FONT`. See [PR details](PR-details.md) for details.

Options > Appearance > Tabs offers **Tab position** (Above navigation / Below navigation) and **Tab style**
(Classic Explorer / Pills). Defaults are below + pills. Pills center inside a shared rounded enclosure made from
the same background as the navigation path and search fields, share the path field's corner radius, and show their
individual surface only on hover/selection. Pills share the manual/system accent and selection-opacity setting with sidebar/configuration navigation;
hover uses 8% accent and selected tabs in inactive split panes use a weaker selection tint. Classic tabs restore the original
rounded cards and outward-curving bottom corners: the active card matches navigation and has a subtle neutral contour to preserve its shape;
inactive cards share the path/search field background, while the surrounding row matches navigation. Text and close controls maintain contrast on these backgrounds. The row appears only with
two or more tabs in a pane, in either style/position. Ctrl+T opens another tab when the row is hidden.
Saved keys are `tab_position=below` (`above` is also accepted) and `tab_style=pills` (`classic` is also accepted).

Sidebar and configuration navigation use a uniform accent-tinted selection, without a left-edge marker.
Selection defaults to 40% of the manual/system accent, identically in light and dark themes.
Appearance > Navigation selection opacity adjusts it from 0% to 100% in 5% steps and also applies to pill focus; the value is saved as
`navigation_selection_opacity=40`. Hover remains at 8%. Each fill blends against that navigation area's actual background.

**Scrollbar colour** (Options > Appearance > Scrollbars): Auto derives the thumb and hover colors from the
desktop highlight when available. Without a desktop color, it uses the selected theme's scrollbar colors.
The input accepts `#RRGGBB` or `#RGB`; click Apply or press Enter. A custom color always takes priority, including
in the System theme, and survives desktop color changes. Click Auto to restore automatic matching. Escape cancels
an unapplied edit. Saved setting: `scrollbar_color=auto` or, for example, `scrollbar_color=#3DAEE9`.

**Surfaces and system highlight** (Options > Appearance): "Unified surfaces" uses the theme's main background
for the sidebar, preview, header, center and status bar (enabled by default). Independent brightness controls adjust
each area from -30% to +30% in 5% steps; 0 restores its original brightness. The header control includes navigation,
pill tabs and the app-drawn title strip. Sidebar and preview brightness are independent. Turning off unified surfaces
restores the selected theme's original surface differences. Terminal content keeps its terminal palette.

"Show separator lines" is **on by default**. The sidebar separator runs continuously from the integrated header
through the sidebar. Turn the option off to hide panel/header/status borders and column dividers.
Resize targets still work and show a highlight on hover; focus and interactive-control outlines stay visible.

"Match system highlight" uses the standard XDG Settings portal's `org.freedesktop.appearance/accent-color` and
follows live changes. The option is available on every distribution and uses optional `libdbus-1.so.3` at runtime.
The desktop must provide xdg-desktop-portal and a backend that exposes its accent color; otherwise the theme accent
is used. In Distrobox this reads the host desktop's session bus. Selecting a manual accent switches system matching
off; toggling system matching off restores the previous manual choice. No KDE/GNOME-specific commands are required.
See the [portal specification](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.Settings.html).

Saved keys (automatically managed by the Appearance controls):
```ini
unified_surfaces=1
show_separators=1
sidebar_brightness=0
preview_brightness=0
header_brightness=0
main_brightness=0
status_brightness=0
system_accent=0
match_system_background=0
previous_theme=Dark
scrollbar_color=auto
header_label=app
header_text=
show_window_controls=1
```

Colour schemes: `~/.config/file-explorer/themes/*.json` (Options → "Create theme from current colours" writes a
starting point). Any key you leave out comes from the `base` theme:
```json
{ "name": "My theme", "base": "dark", "colors": { "bg": "#1b1b20", "accent": "#ff8800", "folder": "#e9b44c" } }
```
Creating a theme from the current appearance bakes in the surface colors, resets their brightness controls to 0,
and turns off Unified surfaces so those colors are not adjusted twice.

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
just updates and what you typed stays. Other shells, or one whose startup files fail before that, get a `cd` typed. Tabs that had it open get it back after a restart, with
a new shell; `terminal_panel_height` is its height. Terminal tabs: Shift+F4 opens a terminal tab in the current
folder (an SSH session in the same remote folder for SFTP connections); right-click for a local or an external terminal. It runs your shell and looks like your default
terminal (xdg-terminal-exec's choice, else $TERMINAL): colours, padding, cursor and scrollback are read from
foot, kitty, Alacritty or Ghostty config and followed live (Options > Appearance > Terminal appearance). Ctrl+Shift+C/V
copy and paste, Shift+PageUp/PageDown scroll back, Ctrl+Shift+W closes, Ctrl+Tab switches tabs. Built on libvterm.

Terminal tabs and panels use the same font family and size as Explorer. Programming ligatures (liga, calt and
dlig) are enabled when supported by that font. JetBrainsMono Nerd Font and its Mono variant retain programming
ligatures through `calt`; the NL (no-ligatures) variant does not provide them. Install a Nerd Font or Symbols Nerd Font for private-use icons;
fontconfig supplies fallback symbols, or set `FE_NERD_FONT` to a font file for an explicit override.
Context menus show Nerd Font action icons beside their labels, with separate checkmarks for toggles. The external
terminal uses U+E795, the filter uses U+EAF1, and sort uses U+F04BA. Apply and Default buttons highlight in the
accent colour on hover.

Right-clicking empty space opens a compact menu with **Views**, **Sort by**, and **Group by** submenus. Hover or
click to open a submenu; Right Arrow opens it, Left Arrow or Escape returns to the parent. Views contains display
layouts and folder-view preferences, Sort by includes direction, and Group by offers None, Date, Type and Size.

Options > Integration > **Terminal executable** accepts an executable name on PATH or an absolute path
(without arguments). Apply validates the executable and confirms the resulting menu label; errors keep the previous
terminal selection. Default clears the selection and confirms the reset. The saved key
is `external_terminal=...`. Before selection, **Open in external shell** opens these settings. After selection,
the menu reads **Open in kitty** (or your chosen executable), with the U+E795 Nerd Font terminal icon. External shells retain the current folder and SSH behaviour for SFTP folders.

The sort button beside the navigation bar's filter button offers Name, Date modified, Type, Size, Date created,
Extension, Permissions and Owner, plus Ascending/Descending. Folder view preferences remember the selection.

Custom file icons: `~/.config/file-explorer/icons/<Name>/` (Options -> Appearance -> "Icon themes folder"). SVGs
named like the built-in icons replace them (`python.svg`, `folder.svg`, `folder-src.svg`...), `ext-<extension>.svg`
sets the icon for any extension, and `theme.conf` (`base=Catppuccin`) picks the set the missing icons come from.
`net-<name>.svg` adds an icon for network locations (offered whichever theme is selected). The folder's README.txt
lists every icon name. See [Custom icons and folder badges](custom-icons.md) for
a complete theme layout, badge sizing, overrides, and fallback rules.

Folder icons keep their base artwork and show small badges at the lower right.
Custom themes can provide these SVGs at the theme's root:

| Filename | Badge |
| --- | --- |
| `folder-remote-smb.svg` | Samba/SMB, including Samba usershares |
| `folder-remote-nfs.svg` | NFS exports |
| `folder-publicshare.svg` | WebDAV and other local shares |
| `emblem-symbolic-link.svg` | Symbolic link (folders and files) |
| `emblem-git.svg` | Git repository folder |

Use transparent backgrounds: these images overlay the base folder rather than
replace it. Simultaneous badges stack upwards on the right in share, symlink,
then Git order. A folder shared through multiple protocols uses SMB before NFS,
then other shares. Git status letters/dots continue to show working-tree changes;
the repository badge identifies the repository folder itself.

Installed desktop themes use the same share and symlink names. The Git repository
badge uses the bundled classic orange Git logo SVG unless a
custom theme supplies `emblem-git.svg`. Missing custom share badges use
`folder-shared` from the custom theme or its base set. Missing symlink badges use
bundled artwork. Installed themes fall back to their folder icon for missing
share artwork, and to bundled artwork for missing symlink emblems. Reload through "Icon themes
folder" after editing SVGs.

Text editing uses a thicker caret in the field's current text color, adapting to
light and dark themes. The caret moves smoothly and gently pulses while idle;
it stays solid during selection. Text selection uses the current accent color softened by a 5% blend toward the
navigation field background, with contrasting dark or white selected text.

Navigation and sidebar icons adapt to light surfaces: pale artwork is darkened
while retaining its hue and shading, and sidebar headings/controls use stronger
foreground contrast. Disconnected network icons remain subdued but readable.
The adjustment is applied when drawing, so custom icon files stay unchanged and
dark surfaces retain their original icon colors.

Navigation path and search fields, the pill enclosure, and unfocused classic tabs
share one neutral background. Focus is indicated by the accent outline.

In Open with, the default-app label uses a 15% lighter accent on dark list
backgrounds. Selected rows retain their contrasting selection text color.

Trash in Quick Access has an **Empty Trash** action in its right-click menu. It empties the Trash independently of the
current folder and active filters. The button at the bottom of the Trash view opens the same confirmation dialog.
The dialog lists up to four names and an additional-item count. Long names end with an ellipsis inside the dialog;
hover over an elided name to see it in full.

The Trash icon in the sidebar, tabs, previews and confirmation dialog uses the selected **File icons** theme. Built-in
sets use their bundled Trash artwork, and a custom theme can supply `trash.svg` (otherwise it uses its bundled base).
**System** follows the desktop icon theme; **System: Theme name** uses that installed theme explicitly. System themes
use `user-trash-full` (or `user-trash-full-symbolic`) when Trash contains items, and `user-trash` when empty. A theme
without a full variant uses its normal bin. Zero-byte files and empty folders count as contents. The system icon's
full/empty state updates after trashing, restoring or emptying; the Empty Trash confirmation shows the full version.
An optional `FE_NERD_FONT` can override built-in and custom theme artwork, but a selected system theme takes precedence.

Terminal ligatures can span foreground-colour changes in Fish syntax highlighting. Shaping keeps operators
together and paints each part in its cell colour; font-weight changes still start a separate shaping run.

For Fish themes that make redirections bold, add this at the bottom of
`~/.config/fish/config.fish`, after theme setup, to keep typed arrows in one font weight:

```fish
if status is-interactive
    set -g fish_color_redirection cyan
end
```

Apply with `source ~/.config/fish/config.fish`. A global setting is needed when a theme
already sets the variable globally; a universal setting alone would be shadowed.
The current session's colours can be inspected with
`set --show fish_color_param fish_color_redirection fish_color_error`.
Verify the font separately with `printf '%s\n' '-> => != == === <= >='`.

For Kitty, edit the host's `~/.config/kitty/kitty.conf` (Fish in a container may
have a separate shell configuration):

```conf
font_family JetBrainsMono Nerd Font Mono
bold_font auto
italic_font auto
bold_italic_font auto
disable_ligatures never
```

Use the installed family's exact name; Kitty's `kitten choose-fonts` can select it.
Remove any `-liga` / `-calt` overrides and open a new Kitty window. Explorer's
terminal keeps using the font selected in its own Appearance settings.
