# UI revamp code review

Reviewed the working changes against upstream commit
`06180f0f4ed9fb98e11d1415662f052234a2e3fb`, including the new source modules and tests.
This is a review of our UI/theme/icon work, not an audit of the whole application.

## Findings fixed

- **Theme-state mismatch:** a follow-system switch could leave the dropdown on a
  manual theme; switching it off could also leave System active. Theme selection
  and following now share one transition, remember the previous theme, and
  normalize older saved settings at startup.
- **Double application of surface settings:** saving the current appearance as a
  new theme could bake in brightness changes and apply them again. Capturing a
  theme now clears those offsets and disables surface unification after capture.
- **Repeated disk access for missing icons:** missing desktop navigation symbols
  were retried on every redraw. Misses now use the existing glyph-cache lifetime
  and are invalidated with normal icon/theme reloads.
- **PNG edge artifacts:** scaling straight RGB and alpha independently could
  darken transparent icon edges. Resampling now interpolates premultiplied colors
  and converts back to the atlas's straight-alpha format.
- **Desktop-monitor shutdown/fallback:** pending portal reads now stop between
  requests, and an unexpected bus disconnect restores local/default settings.
  Local KDE colors remain usable without libdbus.
- **Unstable change comparisons:** desktop state comparisons no longer include
  structure padding; config stamps use ordered mixing rather than XOR cancellation
  when GTK 3 and GTK 4 files change together.
- **Scrollbar consistency:** theme/desktop/default/custom precedence is applied in
  the palette, rather than separately in each scrollbar renderer. Strict hex
  parsing is shared by the settings file and UI. Automatic black/white accents
  get a visibility fallback; explicit custom colors are preserved exactly.

## Project conventions and design

The implementation keeps the creator's C unity-build layout, static functions,
plain structs, named palette fields, bounded arrays, existing worker/main queue,
and existing SDL atlas/texture caches. No new toolkit, runtime command execution,
framework, or build dependency was introduced. Source remains ASCII as required
by `build.sh`; settings continue to use the original per-window merge mechanism.

Responsibilities are separated by purpose: palette/state transitions in theme
code, band geometry in tab preferences, icon discovery/inheritance in system
icons, raster conversion in the icon renderer, and portal I/O in one worker.
Above/below navigation and classic/pill modes share geometry and input bounds.
Integrated controls now share the first header row, with width reservations removed when hidden;
label/hostname/custom-text settings live in a small renderer-independent preference module.
Existing text fields and buttons are reused for the custom scrollbar color.

This applies KISS, avoids duplicated state transitions and drawing logic, and
uses single-responsibility boundaries where useful. SOLID principles are applied
where they fit this procedural C project; adding class hierarchies or generic
service interfaces would not improve consistency with the original codebase.

## Validation

Standalone GCC checks pass with `-Wall -Wextra -Werror`:

- Follow-system/dropdown synchronization, previous-theme restoration and old
  config compatibility.
- Scrollbar automatic fallback, strict hex parsing, custom override precedence,
  desktop color changes, and extreme black/white accents.
- Captured-theme round trips, independent/reversible surface brightness, all
  tab-style/position combinations, single-tab band hiding and fractional scales.
- Actual libdbus message decoding, palette/tint fallback and tab text contrast.
- Installed icon discovery, inheritance cycles, user precedence, SVG/currentColor
  and PNG rendering, and transparent-edge interpolation.

The existing native `--selftest-m6` and `--selftest-m9` suites also contain input,
layout and settings-persistence regressions for these features. They have not
been run here because SDL3, FreeType, HarfBuzz, Fontconfig, libarchive, libvterm
and FUSE development packages are missing. Consequently the full application is
not build-verified here, and screenshots, live portal changes, actual Reversal
rendering, window controls, and X11/Wayland interaction still require a native
Fedora build before merging or release. Sanitizer validation is also pending;
the local ASan runtime is unavailable.

## 2026-11-14 menu, terminal and Trash follow-up

- Menu subgroups now use the existing Menu storage with a saved parent and rebased
  shortcut-string pointers; hover/click/arrow-key navigation shares action dispatch.
- Terminal selection uses one executable field and direct process arguments. Apply
  rejects missing/non-executable paths, preserves the previous value on failure,
  and displays feedback above the Options sheet.
- Terminal shaping enables liga/calt/dlig, retains the terminal cell grid and
  paints glyph slices in each cell's colour. Font/weight boundaries remain explicit.
  Fish redirection bold styling was confirmed as a cause of unjoined typed arrows;
  the configuration workaround is documented rather than changing the user's theme.
- Empty Trash reads its own directory, includes hidden/zero-byte items and preserves
  the existing confirmation/cancellation path. Full/empty icon caches update with
  measured contents and directory-watch events; explicit artwork overrides remain.
- Standalone appearance/icon suites and isolated terminal-rendering checks passed.
  Full native compilation, screenshots and the expanded m7/m8/m9/m11 regression
  suites remain pending in the Fedora environment; see [Checkpoint](checkpoint.md).
