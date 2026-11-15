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
  show File-Explorer, the hostname, custom text, or nothing. The original app
  logo appears beside a visible label in the active accent colour and has its
  own visibility switch. Hiding the label gives its row's height to the left
  sidebar while keeping window controls on the content's top row. Desktop
  title-bar mode remains available, and older title-bar settings remain
  compatible.

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
  and triple-click editing. Settings fields show a caret only while focused;
  opening Options leaves search unfocused, and clicking another control clears
  its caret. After input stops, focused carets use a slow, soft blink. Clickable
  path segments keep a pointer cursor.
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
  those surfaces have been queued. See "Vulkan glyph atlas fix" below.
- **Trash:** Empty Trash reads its own directory, including hidden and
  zero-byte items, regardless of the active pane. Confirmation and cancellation
  use the existing dialog, which elides long names within each row. Trash artwork
  follows the selected file icon theme across the app: bundled and custom sets
  use their own icons, while system themes use `user-trash` and, when available,
  `user-trash-full`. Full/empty icon caches follow measured contents and
  directory-watch events.

### Network locations

- Network now contains saved servers, detected system mounts, local SMB/NFS
  exports, and configured rclone remotes in one section. The Network menu can
  rescan locations and open rclone configuration. Configured NFS, SMB, and
  rclone mount entries remain available after unmounting.
- Connect to server offers GVfs, KDE KIO Fuse, or an existing system NFS/SMB
  mount when the required backend is available. The selected method is saved
  per server; older saved entries default to GVfs. The form scrolls within a
  fixed dialog while keeping its action buttons visible.
- rclone mounts and configured remotes share one sidebar row even when mount
  discovery runs before the rclone status poll. A transient empty remote list
  during unmount does not remove the reconnect row. The edit action uses a
  Nerd Font pencil when available, with a drawn pencil fallback.
- Generic rclone icons follow the selected file icon theme. Custom themes can
  override a remote's icon with `net-<remote>.svg`, `net-<backend>.svg`, or
  `net-<default-icon>.svg`, in that priority order; provider logos remain.
- The old immutable-distros guide was removed; the active connection-method
  and mount behavior is documented in `docs/settings.md` and
  `docs/integration.md`.

## Vulkan glyph atlas fix

Rapid terminal output kept folder and preview surfaces flickering on the Vulkan
renderer. The shared glyph atlas was uploaded while geometry that sampled it was
already queued or in flight. SDL's Vulkan renderer ends the current render pass
for a texture upload, and an in-flight frame could still sample the texture
being changed, so a frame could present partial folder or preview rows.

Main changes:

- Vulkan uses one atlas texture per SDL frame slot. The slot count follows
  `SDL_PROP_RENDERER_VULKAN_SWAPCHAIN_IMAGE_COUNT_NUMBER`, which SDL exposes as
  its swapchain image count / potential frames in flight ([SDL renderer
  properties](https://wiki.libsdl.org/SDL3/SDL_GetRendererProperties)). Slots
  advance with SDL's command-buffer rotation, whose fence is waited before
  reuse. The ring is rebuilt if SDL reports a different slot count.
- A frame that needs unknown glyphs first draws a discarded discovery pass.
  That pass fills the CPU atlas and the glyph caches. The complete atlas is
  then uploaded once to the current slot, and the presented pass is drawn
  again without any texture update.
- A current slot with an up-to-date atlas takes one pass and no upload. A slot
  whose copy is stale after startup, an atlas reset, or a ring rebuild is
  uploaded before the frame that uses it, even if that frame adds no glyphs.
- The Vulkan path no longer calls `SDL_FlushRenderer` or creates and destroys a
  texture per frame, avoiding that per-frame texture lifecycle and its
  synchronization cost.
- The non-Vulkan path keeps its incremental dirty-rectangle uploads.
- Vulkan now draws the frame to a reusable offscreen target before copying the
  finished image to the swapchain. Discovery and redraw passes therefore do not
  reach the window, including a redraw that adds glyphs after switching tabs.
  The target is recreated when the window changes size. If it cannot be created,
  drawing falls back to the direct path. `FE_VK_TARGET=0` selects that direct
  path explicitly for comparison; it does not change the OpenGL path.

Improvements:

- Folder, sidebar, and preview surfaces no longer blink while terminal output
  adds glyphs; the user confirmed no flicker after the change.
- The sidebar separator is drawn once after the header and sidebar, keeping its
  one-pixel edge continuous across both. Tab icons use the tab's surface color
  for contrast adjustments.
- An atlas update is one full upload in a frame that needs new glyphs instead
  of a render-pass break between queued surfaces.
- No texture is created or destroyed in the steady-state frame path, avoiding
  per-frame allocation and destruction work.
- Each slot uses 4 MiB at 1024 squared or 16 MiB at 2048 squared; total atlas
  memory is per-slot memory multiplied by the slot count. Only frames that
  discover glyphs or need to catch a stale slot pay for the extra pass/upload.

M10 checks that geometry queued before and after an atlas publish survives and
that publishing updates the selected ring slot. It also resets the atlas after
idle periods and captures rendered folder-row text to catch blank output. This
is a timing-sensitive smoke check, not a deterministic reproduction of the
terminal typing race.

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

- The Fedora `fedora-build` release build and offscreen M9/M10 self-tests
  passed after follow-up commit `9830b2b`, including the rclone
  mount-transition, settings focus, and hidden-header layout checks. M7
  passed after the network/icon changes. The standalone text-caret animation
  test passed, and `git diff --check` found no whitespace errors.
- Fedora `fedora-build` Distrobox completed a release `bash build.sh` after
  the connected-submenu fix and after the terminal draw-order fix.
- Offscreen native M6, M8, M9, and M11 self-tests passed after that fix.
  Before it, M6 through M11 passed for the rebase. M6 covers tab layout/drops,
  M8 Trash, M9 settings/menus/header, and M11 terminal behavior.
- Expanded M9 checks cover parent-to-child hover, switching submenu groups,
  clicking child actions without Escape, and keyboard close behavior.
- Native M11 and M6 passed again after the terminal draw-order fix. The F4
  panel screenshot from M11 still shows the folder and sidebar correctly.
- The Fedora `fedora-build` release build passed on the frame-slotted atlas
  commit. Native M6, M8, M9, M10, and M11 passed with the Vulkan renderer;
  M10 also passed with `FE_VK_TARGET=1` and with OpenGL before offscreen rendering
  became the default. M10 checks geometry
  queued across an atlas publish and captures folder-row text after atlas
  resets. The latter is a timing-sensitive smoke check, not a deterministic
  reproduction of rapid terminal output.
- The user confirmed that the frame-slotted atlas change stopped folder and
  preview flicker during rapid terminal output on their Vulkan setup.
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
