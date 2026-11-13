# Finder / Dolphin UI revamp

The native C/SDL3 app keeps its existing file operations, split panes and keyboard
navigation. The header has no logo; its left label defaults to File-Explorer and
can show the machine hostname, custom text, or nothing.

## Try the desktop look

Build following [Building](building.md), then open Options > Appearance:

- **Theme > System** follows the desktop palette where available. KDE colors are
  read through the Settings portal or shared `kdeglobals`. Other desktops use the
  standard light/dark preference with a subtle accent tint; exact full-palette
  matching is unavailable when the desktop does not expose those colors.
- **Follow system appearance** automatically selects System in the theme dropdown.
  Turning it off restores the previous theme; selecting another theme turns it off.
- **Match system highlight** follows the desktop accent independently.
- **File icons > System** follows the desktop's current icon theme, including
  Reversal. **System: Reversal** selects that installed theme explicitly. Missing
  icons fall back through theme inheritance, hicolor, then the bundled icon set.
- **Unified surfaces** uses one background across the sidebar, header, file area
  and status bar. Independent brightness controls go from -30% to +30% in 5% steps.
- **Show separator lines** is on by default; switch it off for the seamless look.
  Resize targets and focus feedback stay usable.
- **Scrollbar colour** defaults to Auto: desktop highlight colors when available,
  otherwise theme defaults. Enter a hex color and press Apply/Enter for a persistent
  override; Auto restores matching and Escape discards an unapplied edit.

All preferences are saved. Creating a theme captures current surface colors,
resets brightness offsets and disables Unified surfaces to avoid double adjustment.
The earlier custom dark preset has been removed.

## Sidebar selection

Quick Access, devices and other built-in sidebar locations use a single flat
accent-tinted selection fill, without a left-edge accent marker. Configuration
navigation uses the same treatment. Selection defaults to 40% accent; Appearance >
Navigation selection opacity adjusts it from 0% to 100% in 5% steps. Hover stays
at 8%. Blending uses each area's actual background, with the same strength in
light and dark themes. Both follow the effective manual/system accent; selected labels adapt
for contrast.

## Tabs

Options > Appearance > Tabs has two independent settings:

- **Tab position:** Above navigation or Below navigation.
- **Tab style:** Classic Explorer or Pills.

The default remains Below navigation + Pills. Pill tabs center as a group within
each pane, inside one rounded enclosure derived by darkening the effective
navigation background. The enclosure follows desktop tint and header brightness;
it is independent of the selection color. They share the path field's 7-unit corner radius. Idle
pills have no individual filled shape; hover uses a subtle highlight, and the
focused pill uses the same accent/selection-strength preference as sidebar and
configuration navigation. Labels and close controls
adapt to keep their contrast on the darker enclosure.

Classic Explorer tabs start at the left and restore the original rounded cards
with outward-curving bottom corners. The active card matches the navigation
background exactly and uses a subtle neutral contour to retain its silhouette;
inactive cards are slightly darker, with a lighter hover. The row background
matches navigation too. Pill tabs share the configured accent and opacity for
focus, including system highlight changes, with weaker fills in inactive panes.
Both tab positions work with either style.
Preferences are saved as `tab_position=above|below` and `tab_style=classic|pills`.

A pane with one tab shows no tab row and reclaims its height for file content.
Open another tab with Ctrl+T or the folder context menu to show the row; Ctrl+W
hides it again when one remains. Split panes decide visibility independently.

## Installed icon themes

Themes are discovered from `$XDG_DATA_HOME/icons` (normally `~/.local/share/icons`),
`~/.icons`, and `$XDG_DATA_DIRS/icons` (normally `/usr/local/share/icons` and
`/usr/share/icons`). Distrobox also checks `/run/host/usr/share/icons` when exposed.
The desktop selection is read from KDE/GTK configuration and the Settings portal.
PNG and SVG icons, declared icon sizes/scales, and inherited themes are supported;
XPM icons are not. Logos, thumbnails and custom network-provider art keep their
existing rendering. Common navigation and device icons use the system set too.

If Reversal is missing from the selector in Distrobox, install it inside the box,
or place it in the shared `~/.local/share/icons` directory. Restart or use
Appearance > Icon themes folder to rescan newly installed themes.

The implementation follows the [freedesktop icon-theme specification](https://specifications.freedesktop.org/icon-theme/latest/).

## Nerd Font option

For bundled/custom file icon modes, an installed Nerd Font can replace file and
folder art with monochrome glyphs:

```sh
FE_NERD_FONT=/absolute/path/to/SymbolsNerdFont-Regular.ttf ./build/file-explorer
```

The selected system icon theme takes priority over this environment variable.
Missing glyphs use the selected bundled/custom SVG set. The text font stays
independent; the font is optional and is never downloaded or bundled.

## Integrated header and window controls

Appearance > Title bar offers **Integrated header** or **Desktop title bar**.
Integrated mode has no separate title strip: tabs above navigation share the
first row with window controls; when tabs are below navigation (or hidden for a
single tab), the navigation row shares that space. Header controls reserve only
the width they actually need. Narrow split panes prioritize the path/tab labels.
Chrome stays seamless when inactive file panels are dimmed.

**Show window controls** can hide minimize/maximize/close and reclaim their space.
This works for decoration-free/tiling setups; header dragging, edge resizing,
shortcuts and the window menu remain usable. In Desktop title bar mode, button
visibility is controlled by the compositor, including setups that provide no
native decorations. The app does not reserve an extra client title row for them.

Window buttons use action icons from the desktop's current icon theme even if
file icons use a bundled set; missing window icons use existing vector fallbacks.
This follows icon-theme shapes rather than importing a KWin/GTK decoration
plugin. See the [icon naming specification](https://specifications.freedesktop.org/icon-naming/latest/).

**Header label** chooses File-Explorer, Hostname, Custom or Hidden. Enter custom
text in **Custom header text** and Apply/Enter; Default restores File-Explorer.
Hostname reflects the machine/container namespace in which the app runs. All
preferences are saved; older title-bar settings stay compatible.

## Validation

Standalone checks run without SDL or a display server:

```sh
cc -std=gnu11 -Wall -Wextra -Werror -Wno-unused-function -Wno-format-truncation tools/appearance-selftest.c -o /tmp/fe-appearance-test -lm -ldl -pthread
/tmp/fe-appearance-test
cc -std=gnu11 -Wall -Wextra -Werror -Wno-unused-function -Wno-format-truncation tools/system-icons-selftest.c -o /tmp/fe-system-icons-test -lm
/tmp/fe-system-icons-test
```

They cover reversible brightness controls, tab settings round-trips, above/below geometry at fractional scales, hover/focus contrast, system
palette/tint fallback, real D-Bus variant decoding, icon discovery, SVG/PNG
rendering, size selection, inheritance cycles and missing icons. Native UI checks
(`--selftest-m6`, `--selftest-m9`, screenshots) still require the full build
libraries, which are unavailable in the editing environment.

A review of these changes and the remaining validation limits is in [UI code review](ui-review.md).
