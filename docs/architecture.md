# Architecture

[README](../README.md) · [Install](install.md) · [Keys](keys.md) · [Settings](settings.md) · [Integration](integration.md) · [Building](building.md) · [Architecture](architecture.md)

- `src/fs.c` – directory scan on worker threads: `getdents64` for names/types, then parallel `statx`
  for size/date with on-screen rows prioritized. Sorting runs off the UI thread.
- `src/draw.c`, `src/font.c` – all shapes and glyphs come from one atlas texture, so a frame is a few
  `SDL_RenderGeometry` calls. The app sleeps when idle (no redraw without events).
- `src/shape.c` – text in right-to-left scripts (Persian, Arabic, Hebrew) or scripts that need shaping
  (Indic, combining marks) is reordered (Unicode bidi) and shaped with HarfBuzz; other text skips both.
- `src/ops.c` – copy/move/trash/delete/restore on dedicated threads: `copy_file_range` (read/write
  fallback), cross-device moves, atomic replace via temp file, conflicts found up front and asked about together,
  cancel removes partial files. Transfers wait for their turn after counting (the UI schedules them in queue order),
  pause between chunks, and can verify each copy (flushed, dropped from the cache, read back, XXH64 compared) before it
  replaces anything.
- `src/search.c` – fuzzy scoring, background folder + file index for Ctrl+P, parallel recursive search
  (stays on one filesystem), search inside files (ripgrep when installed - it also skips what `.gitignore` excludes -
  otherwise a built-in parallel search that skips `.git`, binary files and files over 50 MB; at most 100 lines per file
  and 20,000 in all), recursive folder sizes (per-folder cache checked by mtime, so unchanged subtrees cost one statx per folder;
  hidden folders only while shown; in network folders one gentle thread: `du` on the server over the shared ssh
  connection for SFTP, walking the mount for the rest), inotify thread.
  Background work runs at idle CPU/IO priority.
- Folders refresh automatically when their contents change (inotify, debounced and throttled by scan cost);
  refreshes load in the background and swap in, so the list never flickers.
- `src/preview.c` – preview worker (latest request wins): stb_image decode + downscale, thumbnail lookup
  (own cache, `~/.cache/thumbnails`), installed `.thumbnailer`s with an 8 s timeout, text and folder summaries.
- `src/diskuse.c` – disk usage map: measures a folder tree on 4 threads into a tree of folders and their biggest files
  (the rest summed), staying on one drive (btrfs subvolumes of the same disk included); the view in `app.c` / `render.c`
  lays it out as a squarified treemap or a sunburst every frame.
- `src/compare.c` – folder compare: walks both trees together on a worker thread and records only what differs, by path
  relative to the two roots; syncs run as one operation copying each item to its own place (`OP_SYNC` in `ops.c`).
- `src/snapshots.c` – previous versions: finds the filesystem a path is on and looks for it in Snapper / Timeshift / ZFS
  snapshots on a worker thread (or as root through the pkexec helper).
- `src/git.c` – finds a folder's repository, runs `git status --porcelain=v2` for the repository on its own threads (on the server over ssh in SFTP folders) and parses it into per-path states, plus remotes, last commit and stashes; reads branch + remote of the subfolders that are repositories.
- `src/app.c` – layout, input, virtualized list, navigation, tabs, dialogs, menus, progress cards, transfer queue, undo.
  `src/input.c` (keyboard/mouse), `src/render.c` (drawing), `src/settings.c` (config files) and
  `src/selftest.c` (self-tests) are included from it.
