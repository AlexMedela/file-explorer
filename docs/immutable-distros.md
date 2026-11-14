# Immutable distros: build and export with Distrobox

These instructions describe the source-build workflow used with Bazzite and a Fedora Distrobox named `fedora-build`. Other immutable desktops can use the same approach with a suitable container and desktop integration.

Build and install inside Distrobox. The exported launcher appears in the host's application menu and runs the application inside that container, using its libraries. Keep the container available.

## First installation

Enter the container from the host:

```bash
distrobox enter fedora-build
```

Change to the source checkout. Install the build dependencies described in [Building](building.md) inside the container, then run:

```bash
bash build.sh
sudo env PREFIX=/usr bash install.sh
distrobox-export --app /usr/share/applications/dev.fileexplorer.desktop
```

Use `PREFIX=/usr` consistently. The launcher installed by the current installer explicitly points to `/usr/bin/file-explorer`, so an older copy elsewhere in `PATH` cannot take precedence.

Return to the host's application menu and launch **File Explorer (on fedora-build)**. The label may vary if you customized the export label. Pin this launcher to the taskbar if desired.

The installer's message about making Explorer the default folder handler is informational. Installing and exporting do not change the default file manager. Do not use the container's suggested `xdg-mime` command to configure the host's default file manager.

## Updating your build

Fully quit File Explorer before updating. Closing its window can leave it running in the tray; use the tray's **Quit** action when necessary.

Inside the same container, from the source checkout:

```bash
bash build.sh
sudo env PREFIX=/usr bash install.sh
```

Launch the app again from the host menu. Re-exporting is normally unnecessary when the existing launcher and installed executable path remain the same.

If the installed desktop launcher changes, run the export command again to update the host's copy:

```bash
distrobox-export --app /usr/share/applications/dev.fileexplorer.desktop
```

## Checking which build opens

Inside Distrobox:

```bash
type -a file-explorer
/usr/bin/file-explorer --new-window
```

An older `/usr/local/bin/file-explorer` can take precedence when you type the bare command `file-explorer`. The explicit `/usr/bin/` command selects the copy installed with `PREFIX=/usr`.

`./build/file-explorer --new-window` runs the binary in the checkout directly. It does not install or export that build.

## Removing an export

Inside Distrobox, while the installed desktop file still exists:

```bash
distrobox-export --app /usr/share/applications/dev.fileexplorer.desktop --delete
distrobox-export --list-apps
```

**Exported apps** are launchers already available on the host. **Exportable apps** are applications still installed inside the container that could be exported. Removing an export does not uninstall the application, so it can remain in an exportable-apps picker.

If the exact exported launcher remains, remove it on the **host**, outside Distrobox:

```bash
rm -- "$HOME/.local/share/applications/fedora-build-dev.fileexplorer.desktop"
```

This filename is specific to the `fedora-build` container. Substitute the actual filename for a differently named container. If the file is already absent, no further deletion is needed. Unpin an obsolete taskbar shortcut separately.

## Cleaning old manual installations

These commands apply to the manually installed copies used in this workflow. For a package-manager installation, use that package manager's uninstall command instead.

First remove the export as described above. Then, **inside Distrobox**, remove old manually installed binaries and desktop launchers:

```bash
sudo rm -f \
  /usr/bin/file-explorer \
  /usr/local/bin/file-explorer \
  /usr/sbin/file-explorer \
  /usr/local/sbin/file-explorer

sudo rm -f \
  /usr/share/applications/dev.fileexplorer.desktop \
  /usr/local/share/applications/dev.fileexplorer.desktop
```

Refresh the exportable-apps list afterward. These commands leave the source checkout and saved settings in place; they do not remove every supporting file installed by `install.sh`.

For a fresh installation, repeat the three commands in **First installation**.

## Application icons and taskbar matching

In **Settings > Appearance > Application icon**, use **Choose image** to select an SVG or PNG. **Use system** restores the desktop icon.

The current implementation matches the running app's ID to the existing exported launcher and updates that launcher's icon reference while preserving its Distrobox command. Original artwork is copied into the host-visible user data directory for native desktop rendering.

After upgrading from a build with the older launcher identity, fully quit and relaunch once. Re-exporting alone may retain previously copied icon files; it is not a general icon-cache reset.

Taskbar matching and icon changes have standalone regression coverage, including the `fedora-build-dev.fileexplorer.desktop` launcher. The final visual behavior still needs verification on the target desktop.

