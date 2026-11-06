# Keyboard and mouse

[README](../README.md) · [Install](install.md) · [Keys](keys.md) · [Settings](settings.md) · [Integration](integration.md) · [Building](building.md) · [Architecture](architecture.md)

Every shortcut can be changed in Options → Keyboard (Ctrl+,).

| Key | Action |
|---|---|
| ↑ ↓ PgUp PgDn Home End | Move (Shift = extend selection, Ctrl = move without selecting) |
| Enter / double-click | Open folder or file with its default app (`mimeapps.list`; console apps such as nvim in a terminal; text files without a known type in the text editor; `xdg-open` otherwise) |
| Backspace, Alt+↑ | Parent folder |
| Alt+← / Alt+→, mouse back/forward | History |
| Ctrl+L, F6, or type `/` `~` | Edit path (Ctrl+Backspace removes a component) |
| Typing | Jump to file by name |
| Ctrl+A / Ctrl+Space / Esc | Select all / toggle / clear |
| Ctrl+H | Show hidden files |
| F5, Ctrl+R | Refresh (also measures folder sizes again from scratch) |
| Click column header | Sort (click again to reverse) |
| Drag on empty space / outside a name | Rubber-band select (Ctrl = add), auto-scrolls at edges |
| Ctrl+T / Ctrl+W / Ctrl+Shift+T | New tab / close tab / reopen closed tab (in the active panel) |
| Ctrl+Tab, Ctrl+PgUp/PgDn, Ctrl+1…9 | Switch tabs within the active panel (Ctrl+9 = last) |
| Middle-click folder or place, Ctrl+Enter | Open folder in a new tab |
| Middle-click tab / double-click tab bar | Close tab / new tab |
| Ctrl+\ / Ctrl+Shift+\, F3, panel split button | **Panels** (like File Pilot): split the active panel right / down; each panel has its own tabs; splits nest; drag dividers to resize |
| Drag a tab | Onto a tab bar = reorder/move, onto a panel = move there, onto a panel edge = new split (snap overlay); Ctrl+drag duplicates |
| Shift+F5 / Shift+F6 | Copy / move the selection to the previously active panel |
| Ctrl+N | New window |
| Right-click a tab | Duplicate, close others/left/right, move to split, open in new window, reopen closed |
| Tab / Shift+Tab | Next / previous panel |
| Ctrl+C / Ctrl+X / Ctrl+V | Copy / cut / paste (interoperates with Nautilus, Dolphin, Thunar…); pasting a copied image (screenshot, browser "Copy image") saves it as a new file |
| F2 | Rename inline (stem pre-selected) |
| Del / Shift+Del | Move to Trash (freedesktop spec) / delete permanently (asks) |
| Ctrl+Z | Undo rename, move, copy, trash, new folder/file |
| Ctrl+J, ⇅ in the status bar, click a progress card | **Transfer queue**: every copy, move, sync and delete in order, each with its bar, speed and time left. Big transfers take turns (Options → "Big transfers": one / two / four at a time, or all at once; small ones never wait); ↑ ↓ pick a row, Ctrl+↑ ↓ (or the arrows) move it in the queue, Space (or ⏸) pauses / resumes, Del cancels, Esc closes. "Verify after copy" here or in Options reads each copied file back from the disk and compares checksums (XXH64): a copy that differs is removed and reported, and a move then keeps its original |
| Pasting onto existing items | **Conflicts** are asked about together before anything is copied: each name with whether yours is newer or older, then **Skip**, **Overwrite** (folders: Merge), **Rename existing** (what's there becomes `name (2)`), **Newer only** (folders merge file by file) or **Keep both** (the new one becomes `name (2)`) for all of them - or **One by one** (with "do this for the rest"). ↑ ↓ or 1-5 pick, Enter applies; the last answer is offered first next time. Moving a folder newer-only leaves the source in place when some of its files were older |
| Ctrl+Shift+N / Ctrl+Alt+N | New folder / new file (then rename) |
| Ctrl+Shift+C | Copy full path(s) as text |
| F4 | Open terminal here (`$TERMINAL`, else `xdg-terminal-exec`, else the first one found: Ptyxis, GNOME Console, Konsole, COSMIC, Xfce, MATE, ... xterm) |
| Right-click, Menu key, Shift+F10 | Context menu |
| Drag a file by its name | Move onto a folder, pane, or sidebar place (Ctrl = copy, Shift = move; other drives copy by default) |
| Drag folders by their name | Onto a panel edge (top, bottom, left, right) = open them in a new split there (snap overlay); onto the empty part of a tab bar = open them as new tabs |
| Drop files from other apps | Copy into the folder under the cursor |
| Ctrl+P | Go to anything: fuzzy match over every folder and file under ~ and mounts (hidden ones skipped), recent folders, places, tabs. Enter on a file shows it in its folder, Ctrl+Enter opens it |
| Ctrl+Shift+P (or type `>`) | Command palette |
| Ctrl+F | Search bar at the bottom of the pane (like File Pilot). Default: instant fuzzy **filter of this folder**; matched letters highlighted. Wildcards (`*.pdf`) work too. Enter = go to the list, Esc = clear |
| **Subfolders** switch / Alt+R | Expanded search: this folder and everything below, streamed in (adding letters narrows instantly; broader queries rescan after a 300 ms pause) |
| Ctrl+Shift+F, **Contents** switch / Alt+T | **Search inside files** (this folder, and below with Subfolders): each matching file with its count and folder, its matching lines under it (line number, match highlighted). ← / → or the chevron fold a file's lines (right-click: fold / unfold all); Enter on a line opens your editor at it; Space / the preview panel shows the file at the match. `in:*.c` in the query limits the files. Options: **Aa** match case (otherwise case only matters when you type a capital), **ab** whole word, **.\*** regular expression (Alt+C / Alt+W / Alt+X). Right-click a folder → "Search inside files here…" |
| Space, Ctrl+I | Preview panel: images, text (line numbers), folders, video/PDF/etc. via installed thumbnailers; drag its edge to resize, double-click to open |
| Ctrl+D | Pin/unpin the folder (under the cursor, or the current one) in Quick Access |
| Sidebar | **Quick Access groups**: drag folders to reorder or onto a group header to pin; right-click to rename, move to another group, new/delete group; click a header to collapse, double-click to rename |
| Ctrl+Shift+E, right-click empty space → Tree view | **Tree view** in Details: → expands a folder in place, ← collapses / jumps to the parent, click the chevron |
| Ctrl+, | **Options**: theme (Dark, Light, Black, Monokai, Nord, your JSON schemes), accent colour, icons, font, size, row spacing, behaviour, defaults, **keyboard shortcuts** (any command, two-step sequences like `Ctrl+K Ctrl+T`), **saved layouts** |
| Ctrl+P → "Layout: …" | Switch to a saved layout (panels, splits, tabs) |
| Ctrl+Shift+A (or `@` in the palette) | **Search tabs**: every open tab in every panel + recently closed ones |
| Sidebar → Devices | USB drives, SD cards and unmounted partitions: click to mount and open, ⏏ to eject (udisks2) |
| Sidebar → Devices (phones) | Phones and cameras on a USB cable (built-in iPhone / MTP / PTP connector, GVfs as a fallback): click to connect and open, ⏏ when done |
| Sidebar → Network, `smb://` / `sftp://` in the path bar | Network locations (GVfs, sshfs, CIFS, NFS); typing a URI mounts it; ⏏ disconnects |
| Ctrl+= / Ctrl+- / Ctrl+0, Ctrl+wheel | Zoom the UI |
| Ctrl+wheel over files, Ctrl+Shift+1…6, status-bar buttons | **Views**: Details, List, Grid (small → extra large icons with thumbnails) |
| Right-click a column header | Show/hide columns (Date created, Extension, Permissions, Owner…), size bars, alternating rows; drag headers to reorder, drag edges to resize |
| Right-click empty space → Group by | Group by date / type / size; click a group header to collapse it |
| Right-click empty space → view options | A tab keeps its **view** (Details, List, icon size) while you browse; new tabs open in the default view (Options → Views; "Each folder keeps its own view" remembers it per folder instead). Sort and grouping are remembered **per folder**; "Use this view in subfolders" / "everywhere" / "Reset folder view" |
| Right-click a file → Open with… | Pick an app (with icons), optionally make it the default. Console apps (`Terminal=true`, e.g. vim, htop) open in a terminal |
| Alt+Enter, right-click → Properties | Size (live totals), dates, owner, editable permissions, opening app, MD5/SHA-256 |
| F2 with several selected | Batch rename: `{name} {ext} {n} {n:3} {date} {time} {parent} {orig}`, find/replace, case, live preview + conflicts, undo |
| Funnel button in the toolbar | Type filter: files/folders only, hide extensions (popup stays open) |
| Sidebar → Trash | Trash view: original location + deletion date, Restore, Delete permanently, Empty Trash |
| Enter on a zip/tar.*/7z/rar/iso… | Browse it like a (read-only) folder; drag items out or "Extract…" to extract; files open via a cache copy |
| Right-click → Compress to ZIP / tar.gz, Extract here | Create / unpack archives (zip-slip safe) |
| Eye button → Git status | **Git status** in repositories: changed files get a letter (M modified, U untracked, A added, R renamed, D deleted, ! conflict), folders a dot for changes inside, ignored items are dimmed; the status bar shows the branch, ↑ahead ↓behind and the number of changes |
| Branch pill next to a folder's name | The folder is a Git repository: its current branch (or the commit when detached), then its remote (`origin`, else the first one) shortened to `host/owner/repo`; in the grid a branch mark on the icon. Read straight from `.git/HEAD` and `.git/config` - no git runs for it |
| Branch chip at the end of the path bar | In a repository: branch, ↑ahead ↓behind, a dot (orange: uncommitted changes, green: clean). Click for the details - repository, folder, upstream, what's to push/pull, changes, stashes, last commit, remotes - and Open remote in browser, Copy remote URL / branch name, Go to repository root, Refresh |
| Ctrl+Shift+U, right-click a drive / folder → Disk usage…, click a drive's usage bar | **Disk usage**: what takes the space, as a **treemap** (nested boxes sized by size, coloured by type) or a **sunburst** (rings outwards from the folder in the middle), with the biggest items listed beside it. Hover for name, size and share; click to select (Delete, Copy, Properties... work as usual), double-click or Enter to go into a folder, Backspace / the middle of the sunburst to go up. Files under 1% of their folder are grouped as "small files". Drives and folders open it in a new tab; Ctrl+Shift+U switches the current tab |
| Ctrl+Shift+K, right-click → Compare with the other panel, a tab's menu → Compare with current tab | **Folder compare**: the two panels side by side are compared, subfolders included. Each side marks its items: **+** only here, **▲** newer here, **▼** older here, **≠** same date but different, **!** a file on one side and a folder on the other, a dot: differences inside; what's the same is dimmed (**Differences only** hides it). Going into a folder on one side takes the other side along. **Sync…** in the bar under each panel: copy new and newer to the other side, both ways, or mirror (the other side's extra items go to the Trash) - with a list of what will happen first. With one panel, Ctrl+Shift+K offers your other tabs, drives and recent folders to compare with |
| "Retry as administrator" on a failed copy / move / delete, "Do it as administrator" when a rename or new folder isn't allowed | Runs just that operation as root through **pkexec** (polkit asks for your password; it's remembered for a few minutes). What already worked isn't repeated; the system's files can't go to your Trash, so a failed Move to Trash offers **Delete as administrator…** (asks first) |
| Properties → **Previous versions** | Older versions of a file or folder kept in snapshots - **Snapper** (btrfs), **Timeshift** (btrfs and rsync) and **ZFS** - newest first, one row per different version (with how many older snapshots have the same one). **Open** the old one, **Restore…** (Replace moves the current one to the Trash, or Keep both puts the old one next to it), and for folders **Compare** opens the old folder next to the current one in a folder compare. Snapshots only root can read (Snapper's default): **Show as administrator** |
| Right-click a drive → **Snapshots…** (or a folder's background menu → Snapshots of this drive) | All the drive's snapshots (Snapper, Timeshift, ZFS), newest first: Snapper's before/after pairs around updates marked, descriptions. **Open** one to browse it read-only - a bar under the list says which snapshot it is, with **Compare with now** (the folder as it was next to how it is, in a folder compare) and **Restore…** (the selection, or the folder, back to where it lives now; the current one goes to the Trash first). **What changed** lists the files an update (a before/after pair), two picked snapshots (Ctrl+click) or one and now differ in (`snapper status`); Enter shows a file in the snapshot. **Create…** / **Delete…** (Del) for Snapper. Snapper's snapshots are root-only by default: **Allow me to browse snapshots** asks for the password once and adds you to the config's `ALLOW_USERS` (with `SYNC_ACL`) |
| F12 | Frame-time overlay |

Every shortcut above that runs a command can be changed in Options → Keyboard; menus and the palette show the
current keys.

The eye button next to the gear in the status bar switches the features that otherwise only have a key:
preview panel, tree view, hidden files, folder sizes, Git status, size bars, alternating rows and the load times ("listed in … ms") on the right of the status bar.

Git status runs `git status` (needs `git`) for the whole repository on its own threads, so every folder of a repository
you've visited shows its badges immediately; it runs again when a folder reloads, when the window gets focus, when
`.git` changes (checked every 2 s) and - so files saved by other apps in folders you aren't looking at show up too -
every 2 s while a repository is on screen (about 2 ms of CPU for a typical project; big repositories are checked less
often, 50x as long as git takes there, and nothing runs while the window is minimised). In SFTP folders (connected
servers, `sftp://`, sshfs mounts) git runs on the server over `ssh` (needs `git` there; `~/.ssh/config` applies):
connecting a server also opens one ssh connection with the password or passphrase you gave, which git reuses until
you disconnect, so nothing is asked again. Folders connected some other way (sshfs, or before the app started) need a
login without prompts (an `ssh-agent`, or a key without a passphrase); disconnecting and connecting again is enough. It is skipped on other network locations, in archives and in the Trash. `core.fsmonitor` is turned off so a repository's config can't start programs.
`git_status=0` in settings (or Options → Behaviour) turns it off.

Disk usage measures on its own threads (the map fills in while it runs) and stays on the drive it started on: other
drives, network mounts, `/proc` and snapshots show in the list as "other drive" and aren't counted, while btrfs subvolumes
of the same disk (`/home` on CachyOS) are. Sizes are space on disk (allocated blocks, hard links once). Going into a
folder reuses the measurement; after Move to Trash / Delete the map updates right away, and when files arrive it says
so - F5 measures again. `disk_usage_style=treemap|sunburst` and `disk_usage_list=0|1` in settings remember the view.

Folder compare goes by date and size (dates 2 s apart count as the same, like FAT keeps them); "Compare contents" in
the Sync… menu also reads files of the same size byte by byte. A sync copies dates along, replaces older copies, merges
folders and never deletes anything except with Mirror, which moves to the Trash. After a copy or a sync in a compared
folder it compares again; F5 does too.

The administrator helper is this program run by pkexec with `--admin-op` (copy, move, delete, sync, rename, mkdir,
listing versions, or adding you to a Snapper config's ALLOW_USERS); `install.sh` installs a polkit action (`dev.fileexplorer.policy`) so the password prompt says what
it's for. Nothing runs as root otherwise.

Opening a matching line from a contents search: if the default app for the file is an editor that takes line numbers
(VS Code / VSCodium / Cursor, Zed, Sublime Text, Kate, KWrite, GNOME Text Editor, gedit, Geany, gVim, Emacs...) it opens
at the line and column; otherwise a terminal editor in `$VISUAL` / `$EDITOR` (nvim, vim, helix, micro, nano, kak, emacs)
opens in a terminal tab at the line; otherwise the file opens normally.
