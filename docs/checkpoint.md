# UI revamp checkpoint — 2026-11-14

Local work is saved on branch `ui/header-tabs-network-polish`. Nothing has been pushed.
Earlier UI work remains listed below; today's updates are also recorded in
[Unreleased changes](../CHANGELOG.md#unreleased) and [Settings](settings.md).

## Completed today

- Nerd Font action icons and the requested terminal/filter/sort glyphs.
- Compact Views, Sort by and Group by submenus, with pointer and keyboard navigation.
- Navigation sort control with eight sort fields and explicit ascending/descending choices.
- One external-terminal executable field, executable validation, Apply/reset feedback,
  accent hover states, persistence and executable-derived Open in labels.
- Embedded terminal font/size follows Explorer. HarfBuzz enables liga/calt/dlig;
  shaped glyphs retain their cell colours across syntax-highlighting colour changes.
- Quick Access Empty Trash reuses the bottom-right button's confirmation flow and
  enumerates the actual Trash independently of the active pane or filters.
- Full/empty Trash artwork, zero-byte-file detection, icon-theme/custom/Nerd Font
  precedence, symbolic fallbacks, and directory watching while browsing elsewhere.
- Added native regression cases for submenus, sorting, terminal settings/shaping,
  Trash operations and artwork; extended the standalone icon suite.

## Terminal finding to retain

The user confirmed printed == / != ligatures work. Fish's global redirection
colour included `--bold`; removing it allowed a two-colour arrow to join.
Colour changes can share a shaped run; changes of font face or bold/italic style
still split it. Fish/Kitty configuration steps are saved in [Settings](settings.md).
No host or container shell configuration was changed by this session.

## Today's verification and remaining limits

- Strict standalone appearance and system-icon checks passed; the latter includes
  distinct full/empty Trash artwork at dialog size.
- An isolated temporary harness compiled the actual terminal run/shaping functions
  against the installed HarfBuzz runtime and checked regular/bold Fira Code:
  ligature glyphs survived colour changes and both cell colours were painted.
- Whitespace and ASCII-source checks passed.
- Full compilation/native UI self-tests remain blocked by missing development
  packages. The existing binary also cannot launch here because libvterm.so.0 is
  unavailable. JetBrains Nerd Font is not installed in this environment.
- In the Fedora build environment, run `bash build.sh debug`, then the native
  `--selftest-m7`, `--selftest-m8`, `--selftest-m9` and `--selftest-m11` suites.
  Visually check submenu placement near edges, the chosen JetBrains Nerd Font,
  coloured terminal input/cursors, terminal executable feedback, and full/empty
  Trash icons in the sidebar and confirmation dialog before release.

## Earlier completed work

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
