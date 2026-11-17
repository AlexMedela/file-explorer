# Changelog

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
