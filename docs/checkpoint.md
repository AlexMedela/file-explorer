# UI revamp checkpoint — 2026-11-13

Historical checkpoint. For the subsequent badge, preview and text-editing changes,
see [Unreleased changes](../CHANGELOG.md#unreleased) and
[Custom icons and folder badges](custom-icons.md).

Local work is saved on branch `ui-revamp-checkpoint`. Nothing has been pushed.
Upstream baseline: `06180f0f4ed9fb98e11d1415662f052234a2e3fb`.

## Completed

- Classic Explorer tabs with original curved corners, navigation-matching active
  fill and a subtle contour; centered pill tabs with a darker navigation-derived
  enclosure. Tabs can be above/below navigation and hide with one tab per pane.
- Integrated first-row header: tabs or navigation share space with themed window
  controls. Controls can be hidden, reclaiming their space. Desktop title-bar mode
  remains available; old Automatic/Explorer's own UI labels are removed.
- Left label: File-Explorer, hostname, custom text or hidden.
- System palette/tint and accent matching; follow-system selects System in the
  dropdown and restores the previous theme when switched off. The custom Darkly
  preset is removed.
- Installed icon themes such as Reversal, inheritance and SVG/PNG support; system
  window-action icons with vector fallbacks; optional Nerd Font glyphs.
- Unified surfaces; independent sidebar/header/main/status brightness; optional
  separator lines, enabled by default.
- Flat sidebar/configuration selection without left-edge markers. Selection
  opacity defaults to 40%, adjustable from 0–100%; hover stays at 8%. Pills share
  the same accent/opacity settings.
- Automatic desktop-accent scrollbar colors with theme fallback and persistent
  custom hex-color override.
- Review fixes for theme-state synchronization, captured-theme double adjustment,
  icon miss caching, transparent PNG resampling and desktop-monitor lifecycle.

## Verification

Both standalone suites passed with GCC `-O2 -Wall -Wextra -Werror`:

```sh
cc -O2 -std=gnu11 -Wall -Wextra -Werror -Wno-unused-function -Wno-format-truncation tools/appearance-selftest.c -o /tmp/fe-appearance-test -lm -ldl -pthread
/tmp/fe-appearance-test
cc -O2 -std=gnu11 -Wall -Wextra -Werror -Wno-unused-function -Wno-format-truncation tools/system-icons-selftest.c -o /tmp/fe-system-icons-test -lm
/tmp/fe-system-icons-test
```

Git whitespace and source ASCII checks also passed.

## Resume here

The full application has **not** been compiled or visually verified in this
environment: SDL3, FreeType, HarfBuzz, Fontconfig, libarchive, libvterm and FUSE
build dependencies are missing. ASan runtime validation is also pending.

In the Fedora Distrobox, build this modified checkout with `bash build.sh` and
run `--selftest-m6` / `--selftest-m9`. Inspect all tab styles/positions, narrow and
split panes, preview on/off, hidden window controls, hostname/custom labels,
Reversal icons and live desktop color changes on the actual desktop. Confirm
window dragging, resizing and button actions on X11/Wayland before release.

Settings and usage: [UI revamp](ui-revamp.md), [Settings](settings.md).
Review findings and limits: [UI review](ui-review.md).
