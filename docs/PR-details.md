# UI revamp, header tabs, network polish, and connected submenus

## Summary

This branch updates the native C/SDL3 file explorer's appearance and desktop
integration while retaining its file operations, split panes, and keyboard
navigation. It was rebased from `06180f0` onto `7698368`; all nine commits
were replayed without skipping or aborting. A follow-up commit, `e8da701`,
fixes the context submenus and records the conflict review.

### Appearance and navigation

- **System appearance:** KDE colors come from the Settings portal or shared
  `kdeglobals`; other desktops use their light/dark preference and an accent
  tint when a full palette is unavailable. Follow system appearance keeps the
  theme dropdown synchronized and restores the previous manual theme when
  switched off. Match system highlight follows the desktop accent separately.
- **Surfaces:** Unified surfaces can give the sidebar, preview, header, file
  area, and status bar one background. Sidebar and preview brightness are
  independently adjustable. Separator lines can be hidden; resize targets and
  focus feedback remain. Scrollbars use the desktop highlight or a theme
  fallback, with a persistent strict-hex custom color override.
- **Selection:** Sidebar and configuration navigation use a flat accent fill.
  Selection opacity defaults to 40% and ranges from 0% to 100%; hover uses 8%.
  Text and icon contrast adapt to each surface.
- **Tabs:** Position (above or below navigation) and style (Classic Explorer or
  Pills) are independent settings. Pills form a centered rounded group on the
  path-field surface. Classic tabs retain curved cards, with the active card
  matching navigation. A pane with one tab hides the tab row and gives its
  height back to file content; split panes decide this independently.
- **Integrated header:** Tabs or navigation share the first row with themed
  window controls. Hiding those controls releases their width. The label can
  show File-Explorer, the hostname, custom text, or nothing. Desktop title-bar
  mode remains available, and older title-bar settings remain compatible.

### Icons, files, menus, terminal, and Trash

- **Installed icon themes:** System follows the current desktop theme; a named
  installed theme such as Reversal can be chosen explicitly. Discovery checks
  XDG icon locations, `~/.icons`, and the exposed host icon directory in
  Distrobox. PNG/SVG size selection and inheritance work, with hicolor and
  bundled fallbacks. XPM is unsupported. The selected system theme takes
  precedence over optional `FE_NERD_FONT`; a missing Nerd Font glyph falls
  back to the selected bundled/custom SVG.
- **File artwork:** Local Samba, NFS, and WebDAV share roots, symlinks, and Git
  repositories get configurable badges. Multiple badges stack on the right.
  SVG files render in grid thumbnails and preview. Window, tray, and launcher
  icons follow the selected artwork while preserving the appropriate fallback.
- **Text editing:** Path, search, and other focused fields have a visible
  animated caret, accent selection, and click, drag, Shift-click, double-click,
  and triple-click editing. Clickable path segments keep a pointer cursor.
- **Context menus:** Views, Sort by, and Group by now use the same connected
  submenu path as Open With. Their rounded joins, mouse travel, click handling,
  keyboard navigation, and side selection near a window edge share one model.
  The toolbar Sort button still opens its standalone sort menu.
- **External terminal:** One executable setting is validated on Apply. Invalid
  paths retain the prior choice and show feedback. The embedded terminal uses
  the Explorer font/size and shapes `liga`, `calt`, and `dlig` while preserving
  the terminal cell grid and each cell's color. Font face and weight boundaries
  remain explicit. Visible terminals draw before folder, sidebar, and preview
  surfaces so new terminal glyphs cannot update their shared texture after
  those surfaces have been queued.
- **Trash:** Empty Trash reads its own directory, including hidden and
  zero-byte items, regardless of the active pane. Confirmation and cancellation
  use the existing dialog. Full/empty icon caches follow measured contents and
  directory-watch events; custom artwork still takes precedence.

## Review findings resolved

- Theme selection and follow-system mode now share one transition and normalize
  older saved settings, avoiding a mismatched dropdown or stuck System choice.
- Saving a custom theme clears brightness offsets and Unified surfaces after
  capture, avoiding double application of those adjustments.
- Missing desktop icon lookups use the existing glyph-cache lifetime instead
  of hitting disk on every redraw. Normal icon/theme reloads invalidate misses.
- PNG resampling interpolates premultiplied color, then converts back to the
  atlas format, avoiding dark edges around transparency.
- Pending portal reads stop during shutdown; a bus disconnect restores local
  or default settings. Local KDE colors still work without libdbus.
- Desktop-state comparisons exclude structure padding, and ordered config
  stamps avoid XOR cancellation when GTK 3 and GTK 4 files change together.
- Scrollbar color precedence lives in the palette; the settings parser and UI
  share strict hex parsing. Automatic black/white accents get a visibility
  fallback, while explicit custom colors remain exact.

The implementation retains the project's C unity build, bounded arrays,
static functions, existing worker/main queue, and SDL atlas/texture caches.
It introduces no new toolkit or build dependency. Source remains ASCII as
required by `build.sh`.

## Rebase conflict trace

| Original | Rebased | Resolution or assessment |
| --- | --- | --- |
| `0340027` | `0dc08eb` | Combined the integrated header with upstream window corners, shadows, folder menu, and settings changes. |
| `00e4eb2` | `631e7af` | Applied cleanly. |
| `9cee880` | `5707a18` | Resolved a tab-layout conflict, largely caused by source formatting. |
| `8725bdf` | `07b7a83` | Applied cleanly. |
| `3471ffb` | `9a0cbf4` | Merged tray, window, and desktop icon sources. |
| `3c38990` | `9c7d1f4` | Applied cleanly. |
| `1365c34` | `3dbc93d` | Combined badges, text editing, window drawing, and watchers. |
| `0c172c8` | `0ae8a1a` | Applied cleanly. |
| `94d0110` | `469bc0b` | Combined menu models, external terminal, and Trash behavior; the connected-submenu correction follows in `e8da701`. |

### Header and tabs

The first replay retained `round_corners` in Options, parsing, writing, and
settings documentation; it retained the shadow and corner mask in `frame_draw`
and combined the optional border with `g_show_separators`. Integrated-header
labels and settings were kept. The folder menu stays at the right of navigation,
and narrow panes hide controls to leave path space. Both tab positions use
`TabBands.content_y` for file content. The later pill-tab commit changed its
bottom spacing from 12 to 16 scaled pixels without replacing that layout.
For clipping or overlaps, inspect `layout_pane` and `layout_panel` in
`src/app.c`, `draw_window_controls` in `src/render.c`, and the Options/settings
code.

### Icons, caret, and watchers

The icon merge retained updates to existing tray icons when their source
changes and kept configurable application artwork and launcher syncing.
Large list/detail icons still use the thumbnail path; smaller folders use
badge-aware drawing. Trace `tray_update`, `window_icon_update`, and
`app_icon_sync` in `src/app.c`, plus `draw_pane` in `src/render.c`, `src/icons.c`,
and `src/app_desktop_icon.c`. `TextCaret` was added without removing UTF-8
editing behavior. Window clear, corner mask, shadow, and text-field frame
bookkeeping were combined. The watcher now observes Trash even while another
folder is open, keeping the existing debounce and pane-refresh behavior.
The full build also exposed and resolved a `units.c`/`rclone.c` include-order
error. Trace `te_set`, `frame_draw`, `watch_sync`, and `watch_process` in
`src/app.c`, with `src/input.c` and `src/text_caret.c`.

### Menus, terminal, and Trash

The initial resolution of `94d0110` preserved Open With but put Views, Sort
by, and Group by on a separate saved-parent menu. That path produced detached
popups and blocked normal mouse travel until Escape. Commit `e8da701` moved
those three groups to the existing connected `g_menu_sub` path. All temporary
saved-parent helpers and state (`g_menu_parent`, `g_menu_building_child`,
`menu_copy`, `menu_is_submenu`, `menu_open_child`, `menu_restore_parent`) were
removed. The three action IDs remain parent-row identifiers and glyph hooks;
each row now supplies a submenu builder. Open With retains its original path.
The folder menu, filter, and sort controls remain outside the header drag area.
The terminal executable is parsed and saved; the menu directs the user to
Integration settings until an executable is selected. Trash enumeration and
its confirmation path remain independent of the visible pane. Trace
`menu_open`, `menu_sub_open`, `menu_activate`, `trash_delete`, and
`watch_process` in `src/app.c`, with `src/input.c`, `src/render.c`,
`src/options.c`, and `src/settings.c`.

The original pre-rebase macro formatting edit at the top of `src/app.c` was
preserved through the rebase and included in the later `e8da701` commit.

To compare the original and rebased commits:

```sh
git range-diff 06180f0..94d0110 7698368..469bc0b
```

The original UI checkpoint extensively reformatted `src/app.c`; function-level
inspection is more useful than its raw whole-file diff when tracing behavior.

## Verification

- Fedora `fedora-build` Distrobox completed a release `bash build.sh` after
  the connected-submenu fix and after the terminal draw-order fix.
- Offscreen native M6, M8, M9, and M11 self-tests passed after that fix.
  Before it, M6 through M11 passed for the rebase. M6 covers tab layout/drops,
  M8 Trash, M9 settings/menus/header, and M11 terminal behavior.
- Expanded M9 checks cover parent-to-child hover, switching submenu groups,
  clicking child actions without Escape, and keyboard close behavior.
- Native M11 and M6 passed again after the terminal draw-order fix. The F4
  panel screenshot from M11 still shows the folder and sidebar correctly.
- Standalone appearance and system-icon suites passed with GCC `-O2`,
  `-Wall -Wextra -Werror`; isolated terminal shaping/rendering checks passed.
- Git whitespace and source ASCII checks passed; no conflict markers or
  unmerged paths remained. The user confirmed that a submenu near a window
  edge opens on the side with space.

M6's tab-drop fixture now opens a second tab because the only tab is hidden.
M9 clears Options search before checking the full scrollable list and restores
Unified surfaces after theme capture intentionally disables it.

## Remaining review

Automated checks do not establish all visual behavior on X11 or Wayland.
Before release, inspect integrated-header dragging, corners and shadows,
live tray/taskbar icon changes, the selected external terminal, and the chosen
desktop icon theme on a real desktop. The M11 environment lacked zsh, so its
shell integration was not exercised. Sanitizer validation remains pending.
