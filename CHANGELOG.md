# Changelog

## 0.12.0

**New**
- Quick Look: Space shows the file under the cursor large over the window, like macOS, Windows' Peek and GNOME's Sushi: pictures fitted to the window, text with line numbers, else its icon. ← / → go to the previous / next file, Space or Esc closes it. The preview panel moved to Ctrl+I alone. Thanks to @pr83-glitch.

- The Save dialog remembers where you saved each type of file: the next PDF starts in the folder the last PDF went to, the next photo where the last photo went. Thanks to @pr83-glitch.

- Properties of a photo (JPEG) shows its EXIF in the Media card: when it was taken, the camera and lens, the exposure (f/1.8 · 1/120 s · ISO 100 · 6.9 mm) and where (GPS coordinates), read by the app itself.

- Properties of an archive shows what it holds: files, folders and the unpacked size (and how much it saves), counted in the background and updated as it goes, so big compressed archives don't make you wait.

- Properties has a Media card for images, videos, music and PDFs: dimensions ("1920 × 1080 pixels"), length, the video and audio codecs, frame rate, stereo / 5.1 and sample rate, a song's bit rate and title / artist / album, a PDF's pages, title and author. Image sizes are read directly; the rest through `ffprobe` (ffmpeg) and `pdfinfo` (poppler) when they're installed, in the background. Thanks to @pr83-glitch.

- Theme > System can use any theme: Options > Appearance > **Light theme** and **Dark theme** pick the ones it switches between as the desktop goes light or dark (Light and Dark by default). Thanks to @pr83-glitch.

- The Snapshots view lists Timeshift snapshots in rsync mode too (on any drive, e.g. ext4), with when each was taken, its comment and its tags (daily, boot…). Timeshift snapshots of either mode now also show up next to Snapper's (a Tool column then says which took each), and can be created or deleted from the view: Timeshift runs as administrator and asks for the password each time. Thanks to @teraflop09-web.

- Browse BorgBackup repositories: folders that are Borg repositories (Borg 1, and Borg 2 beta) get a "Borg backup" pill; opening one shows its backups as dated folders, read-only (through `borg mount`, which needs borg's FUSE support, e.g. `python-pyfuse3`). Encrypted repositories ask for the passphrase, which can be kept in the system keyring. Inside a backup, "Compare with now" and "Restore…" work like in snapshots; copy or drag out anything else. Repositories in SFTP folders are read by borg on the server. "Add to sidebar" keeps a repository under a new Backups section, with its own name and icon, opened with a click; Borg 2 ones can stay mounted while the app runs. Thanks to @teraflop09-web.

- Run AppImages, programs and scripts: opening an executable file asks "Run", "Run in terminal", "Open" (scripts, also `.sh` files without `#!`) or "Cancel", with "Don't ask again" (Options > Behaviour > Programs). A downloaded AppImage without the executable bit offers "Make executable and run". Right-click > Run / Run in terminal too.

- Compress to tar.xz, tar.zst and 7z as well as ZIP and tar.gz: right-click > Compress to lists the formats, and Custom… picks the archive's name, its format and how hard to compress (Fast / Normal / Best, or the exact level), remembered for next time. xz and zstd use every core. Thanks to @teejee2008.

- "Folders first" can be turned off, to mix folders and files (Sort menus, Options > Views); with it on, Group by date puts the folders in their own group on top. The toolbar's sort button offers the same choices as the right-click Sort by. Thanks to @teraflop09-web.

- A refreshed look: the colours and accent follow the desktop's light / dark setting (KDE colour schemes too); Options > Appearance can unify the surfaces, make the sidebar, header, file area, preview and status bar lighter or darker, hide the separator lines, set the selection's strength and the scrollbar colour; tabs come as cards or pills and their row hides while a panel has one tab; the window buttons sit in the tab row, with the "Explorer" label and logo optional; Views, Sort by and Group by are submenus; text fields select with the mouse and long labels show in full on hover. Thanks to @juangiler.

- Network lists saved servers, mounted shares, the SMB / NFS shares of this computer and rclone remotes together; a server can be connected through GVfs, KDE's KIO Fuse or a system mount, remembered per server, and stays listed to reconnect after unmounting. Thanks to @juangiler.

- Smooth scrolling everywhere: Options, Properties, Open with, menus, Quick Look text and the rename list ease like the file panes; a quick run of wheel notches scrolls further each time, and a touchpad follows the fingers and glides on after a flick.

- Compare with… opens a small dialog with both folders: the other one can be typed or edited, with folder suggestions as you type, instead of a fixed list of tabs and recent folders.

- Menu switches with an icon (View, Features, Cloud's Connect at login…) show "on" by turning the icon accent-coloured instead of a check mark.

**Fixed**
- Inside a Timeshift snapshot the bar with "Compare with now" and "Restore…" shows, as it does for Snapper and ZFS.
- Dragging files to other apps on Wayland: a drag can switch workspaces (Super / Alt hands it to the desktop, so a workspace shortcut no longer drops it where the pointer is), and after a drop into an app that never says it's done, clicks no longer start a selection box that follows the mouse (Esc also ends such a drag). Thanks to @pr83-glitch.
- In the Save dialog (the app as file chooser), Ctrl+L / F6 edits the path while the file name has the keyboard, and Alt+←/→/↑, Ctrl+H and F5 work there without losing the typed name. Thanks to @pr83-glitch.
- In the Save dialog, selecting a file puts its name in the file name field (its name without the extension selected), to save over it or name the new file after it; a name typed afterwards stays until another file is selected. The name can also be a path (`sub/name`, as a file in the tree view fills in, or a full one) into a folder that exists. Thanks to @pr83-glitch.
- Extract here with several archives selected unpacks every one of them (each into its own folder), not only the last one clicked; the menu says how many. Thanks to @pr83-glitch.
- With the terminal panel (F4) open, the status bar keeps the folder's item count, size slider and view buttons (it showed the terminal's details over them, while clicks still went to the hidden buttons), and a prompt that starts with an empty line (starship's `add_newline`) starts on the panel's top row.
- On small or scaled screens (e.g. 200% on a Surface) a window snapped to half the screen fits: the minimum window size is capped at half the display. Thanks to @zeropaulsy.
- Empty Trash works whichever panel is active and its icon shows whether the trash is full; a cancelled copy or move stays at the progress it reached, marked cancelled; failed compressions say why and leave no half-written archive, and non-ASCII names inside archives are kept; the free space of `/` is right; network mounts whose path has spaces no longer show up twice; text on Vulkan no longer flickers or garbles. Thanks to @juangiler.
- Split right / down no longer squeezes a panel below a usable size: the split goes to a panel that has room, or the button dims and says so when none has.
- The search field in Options no longer looks active as soon as it opens: it takes the keyboard when clicked or typed into.
- The app no longer quits silently when a helper program it writes to has already exited.

## 0.11.2

**New**
- Rounded window corners and a drop shadow, like GTK apps (Options > Appearance > Rounded corners, off by default, takes a restart). Square and without shadow when maximised or snapped. On X11 it needs a compositor. Thanks to @zeropaulsy.
- Open folders with other apps: right-click a folder → "Open with…", or empty space → "Open this folder with…" for the folder you are in, to open a project in your editor, terminal or game engine. The app starts inside the folder. "Open with" is a submenu (also for files): the apps you picked for that type before through "Other app…" come first, then the apps for that type. Thanks to @pr83-glitch.
- The terminal panel has a close button (×, top right) and a "Hide terminal panel" entry in its right-click menu, so it can be closed without F4. Thanks to @pr83-glitch.

**Fixed**
- "Open in external terminal" opens in that folder with every terminal, also Ghostty and terminals that hand the window to one already running (the folder is passed with the terminal's own option, e.g. `--working-directory`). Thanks to @pr83-glitch.
- The scrollbars in Options, Open with and Properties can be dragged (and clicking beside the thumb jumps a page), not only scrolled with the wheel. Thanks to @juangiler.

## 0.11.1

**New**
- Ctrl+wheel over the files makes them bigger or smaller and keeps the view, instead of switching between Details, List and icons. Details and List get bigger icons (with thumbnails from 32 px) and taller rows; the grid's icons grow in finer steps. A size slider in the status bar, next to the view buttons, does the same. Each view keeps the size you last picked for it (also in new tabs and after a restart) until you change it.
- A ⋯ button next to the type filter opens the folder's menu (new folder, paste, view, sort, properties…), handy when the folder is so full there's no empty space left to right-click. Its "Properties", "Copy path", "Pin this folder" and "Paste" are about the folder itself, also when something is selected.

**Fixed**
- Dragging files out to other apps on X11 (LXQt, Xfce, MATE, ...) when the press on the file is also the click that brings the window to the front: the drag no longer stops at the window's edge and lands in the wrong app, or nowhere.
- Folders on network drives (SSH/SFTP, SMB, NFS, cloud) get their sizes too instead of showing "…" forever. In SSH folders the server measures them itself (one quick `du` over the connection); elsewhere they're counted in the background by one gentle thread. A folder that can't be measured shows "—". Thanks to @pr83-glitch.

## 0.11.0

**Changed**
- The automatic renderer is OpenGL: resizing the window no longer lags behind the pointer, and fewer blinks. `renderer=vulkan` in the settings file brings Vulkan back (it can take less memory on Mesa).

**New**
- Starting File Explorer again from the app menu or a dock opens a new window instead of bringing the old one to the front (a window hidden in the tray still comes back). Folders opened from other apps still become tabs in the running window.
- The card next to the pointer while dragging shows what you're dragging: up to three items with their icons and names, "+ N items" for the rest, and what the drop will do.
- The pointer turns into a grabbing hand as soon as you drag files, a tab or a sidebar place (from your cursor theme).
- Drag files out of the window into other apps: upload in a browser or chat app, open in an editor, drop into another file manager (move / copy / link by the same keys and drop setting). Flatpak and Snap apps get the files through the desktop's document portal. The item card keeps following the pointer outside the window. Works on Wayland and X11.
- Terminal panel (F4), like Dolphin's: a shell under the folder, one per tab, that follows the folder both ways - open a folder and the shell goes there (quietly in bash, zsh and fish: the prompt just updates, nothing is typed), `cd` in the shell and the folder follows. Ctrl+Shift+F4 moves the keyboard between them; drag the line above it to resize; tabs that had it open get it back after a restart. Terminal tabs moved to Shift+F4.
- Dropping files can ask what to do: turn on Options > Behaviour > "Ask what to do when dropping files" for a Move / Copy / Link here menu after each drop. Ctrl+Shift while dropping makes links.
- Options > Updates: the version, how this copy updates, and "Check now". The AppImage updates itself: a card offers a new version once, "Update now" downloads and checks it, "Restart now" switches to it (or turn on downloading by itself).

**Fixed**
- No more blink when opening a folder or going back up through the address bar: the list switches once, with sizes and dates already filled in.

## 0.10.1

**New**
- The file chooser shows the Cloud section too: open from or save to an rclone remote directly.
- Tree view: right-click a folder to create a new folder or file inside it.
- Persian, Arabic and Hebrew names read right to left with joined letters; Indic scripts and combining marks are shaped properly.

**Fixed**
- Less blinking: refreshes keep folder sizes and scroll position, and X11 no longer bypasses the compositor.
- The taskbar icon no longer turns into the app's name on LXQt, and window titles with non-ASCII characters reach X11 panels.

## 0.10.0

**Changed**
- Views stay with the tab while browsing, and new tabs open in the default view. To get the old behaviour back (each folder keeps its own view), turn it on in Options.

**New**
- Paste a copied image (a screenshot, or "Copy image" in a browser) into the current folder as a new file.
- Drag folders onto a panel edge to open them in a new split, or onto the empty part of a tab bar to open them as tabs.
- Git status in SFTP folders and on sshfs mounts: git runs on the server over ssh. Connecting with a password or passphrase works without an ssh agent.
- Tree view: Ctrl+V pastes into the selected folder, a selected row drags the whole selection, and items can be dropped onto the tab's own folder.
- A hand pointer over everything clickable and an I-beam over text fields.

**Fixed**
- Disk usage of `/` on btrfs measures the other subvolumes (`/home`, ...) again.
- Disk usage of a compressed btrfs drive also shows how much it really takes up on disk.
- Folders on `/proc`, `/sys` and other kernel filesystems show no size instead of 128 TiB.
- Drives appear in the sidebar right away instead of after the phone scan.
- Resizing a column moves only the edge you grab.
- A saved server whose address includes a folder opens that folder instead of the server's root.
- Clicking a drive's usage bar opens the drive, and the bar no longer grows on hover.

## 0.9.0

First public release.
