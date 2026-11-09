# Changelog

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
