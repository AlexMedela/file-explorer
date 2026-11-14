# Rebase conflict review — 2026-11-14

This records the manual resolutions made while rebasing
`ui/header-tabs-network-polish` from `06180f0` onto `7698368`. The replay
finished at `469bc0b`. Nine commits were replayed; none was skipped or aborted.
The original commit IDs remain available for comparison.

At completion, the only tracked-file worktree change was the pre-existing,
unstaged macro formatting edit at the top of `src/app.c`. Its saved copy is
`/tmp/file-explorer-preexisting-app-worktree.patch`. That edit was kept out of
the rebased commits. The report itself is a new, uncommitted documentation file.

## Commit map

| Original | Rebased | Conflict assessment |
| --- | --- | --- |
| `0340027` | `0dc08eb` | Substantive: header UI met upstream window, folder-menu and settings changes. |
| `00e4eb2` | `631e7af` | Applied cleanly. |
| `9cee880` | `5707a18` | One tab-layout conflict, mostly caused by source formatting. |
| `8725bdf` | `07b7a83` | Applied cleanly. |
| `3471ffb` | `9a0cbf4` | Icon-source conflict in tray, window and desktop icon updates. |
| `3c38990` | `9c7d1f4` | Applied cleanly. |
| `1365c34` | `3dbc93d` | Substantive overlaps in badges, text editing, window drawing and watchers. |
| `0c172c8` | `0ae8a1a` | Applied cleanly. |
| `94d0110` | `469bc0b` | Highest-risk merge: two menu models plus terminal and Trash behavior. |

## Manual resolutions and regression paths

### Header, tabs and upstream window behavior (`0dc08eb`, `5707a18`)

The first replayed commit conflicted with upstream's rounded corners and shadow,
the folder menu button, draggable Options scrollbars and the old `title_bar=auto`
setting. The merge kept `round_corners` in the Options definition, parser, writer
and documentation. It kept the existing shadow and corner mask in `frame_draw`,
and combined the optional border with `g_show_separators`. Integrated-header
settings and labels from the UI commit were retained. The folder menu button was
kept at the right of navigation; narrow panes hide controls to leave path space.
Tab content starts at `TabBands.content_y`, so above- and below-navigation modes
share the same content bounds. The later pill-tab commit changed its bottom
spacing from 12 to 16 scaled pixels without replacing that layout.

Trace: `src/app.c` (`layout_pane`, `layout_panel`, `frame_draw`), `src/render.c`
(`draw_window_controls`), `src/options.c`, `src/settings.c`, and
[`settings.md`](settings.md). If a split pane has a clipped path or overlapping
navigation controls, start with `layout_pane` and this first resolution.

### Icon theme and badge integration (`9a0cbf4`, `3dbc93d`)

The icon commit switched the tray and SDL window icon from the bundled logo to
the selected folder icon and updated an existing tray icon when the source
changed. The later badge/preview commit added configurable application artwork
and desktop launcher syncing; its `app_icon_raster` and launcher update logic
were retained. Large list/detail icons still use the thumbnail path, while
smaller folders use the new badge-aware drawing function. Missing icons retain
the normal fallback path.

Trace: `src/app.c` (`tray_update`, `window_icon_update`, `app_icon_sync`),
`src/render.c` (`draw_pane`), `src/icons.c`, and `src/app_desktop_icon.c`. If a
window, tray or launcher icon differs from the selected theme, inspect the
original `3471ffb` alongside rebased `9a0cbf4` and `3dbc93d`.

### Text editing, window drawing and folder watchers (`3dbc93d`)

`TextCaret` animation state was added to the existing text editor without
dropping its UTF-8 editing behavior. The window clear, corner mask and shadow
path were retained while text-field frame bookkeeping was added. The new
directory watcher helper also watches Trash even when another folder is open;
the existing debounce and pane refresh behavior remains. The include order for
`units.c` and `rclone.c` was restored after the full build exposed an ordering
error.

Trace: `src/app.c` (`te_set`, `frame_draw`, `watch_sync`, `watch_process`),
`src/input.c`, and `src/text_caret.c`.

### Menus, external terminal and Trash (`469bc0b`)

This was the most complex resolution. The existing Open With submenu and its
selection-target behavior were retained. The first rebase resolution put Views,
Sort by and Group by on a separate saved-parent menu path. That left those
submenus unresponsive to normal mouse travel and drew them as separate popups.
The subsequent fix moved all three onto the same connected `g_menu_sub` path as
Open With; their callback builders now share hover, click, keyboard and rounded
join handling. The folder menu button remains beside filter and sort controls;
all three are excluded from header dragging. The external-terminal setting is
parsed and saved, and the menu opens Integration settings until an executable
is selected.

The original `94d0110` source was audited for every use of its temporary menu
model: `g_menu_parent`, `g_menu_building_child`, `menu_copy`,
`menu_is_submenu`, `menu_open_child` and `menu_restore_parent`. Its dispatcher
recognized exactly three actions: `ACT_MENU_VIEWS`, `ACT_MENU_SORT` and
`ACT_MENU_GROUP`. All uses of that temporary state and those helpers have been
removed from the current source. The three action IDs remain as parent-row
identifiers and for their menu glyphs; each parent row now supplies a callback
to the existing `g_menu_sub` builder. The toolbar Sort button still opens its
standalone sort menu, using the same sort-item builder. Open With continues to
use `g_menu_sub`; other context menus remain standalone menus.

Empty Trash reads the Trash directory itself, including hidden and zero-byte
entries, even when the active pane shows another folder. The sidebar action and
full/empty icon updates were retained. Confirmation and cancellation still use
the existing dialog path.

Trace: `src/app.c` (`menu_open`, `menu_sub_open`, `menu_activate`,
`trash_delete`, `watch_process`), `src/input.c`, `src/render.c`, `src/options.c`,
and `src/settings.c`. For a menu regression, compare original `94d0110` with
rebased `469bc0b`, then inspect the later connected-submenu fix. Mouse travel
and child activation are covered in M9. After the fix, the user observed that
opening a menu near the window edge places its submenu on the side with space.

## Checks performed after resolution

- Fedora `fedora-build` Distrobox: `bash build.sh` completed a release build.
- With `SDL_VIDEODRIVER=offscreen`, native `--selftest-m6` through
  `--selftest-m11` all passed. M6 covers tab drops and layout; M8 covers Trash;
  M9 covers settings, menus and the header; M11 covers terminal behavior.
- `tools/appearance-selftest.c` and `tools/system-icons-selftest.c` compiled
  with GCC `-O2 -std=gnu11 -Wall -Wextra -Werror` and passed.
- `git diff --check` passed, and no conflict markers or unmerged paths remained.

Three self-test setup assumptions were adjusted during the replay. M6 now adds
a second tab before dropping onto a tab, because a panel's only tab is hidden.
M9 clears the Options search before testing the scrollable full list, and
restores unified surfaces after theme capture intentionally turns them off.
These changes are in `src/selftest.c` in rebased `469bc0b`.

After a user reported that Views, Sort by and Group by required Escape to
release the mouse, the connected-submenu fix completed a Fedora release build.
Native M9 passed with new checks for hover from parent to child, switching
between submenu groups, clicking child actions without Escape, and keyboard
close behavior. M6, M8 and M11 also passed after this fix. The original
Open With submenu remains on the same path.

## Remaining review limits

Automated checks do not establish all visual behavior on X11 or Wayland. The
user confirmed submenu side selection near a window edge after the fix. Before
a release, inspect integrated-header dragging, rounded corners and shadows,
live tray/taskbar icon changes, and the selected external terminal on a real
desktop. The native M11
run reported that zsh was not installed, so its shell integration was not
exercised.

To reproduce the commit comparison:

```sh
git range-diff 06180f0..94d0110 7698368..469bc0b
```

`src/app.c` was extensively reformatted in the original UI checkpoint. Raw
whole-file diffs therefore include substantial formatting noise; inspect the
named functions and commit pairs above when tracing a behavioral regression.
