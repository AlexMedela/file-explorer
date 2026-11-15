# Custom icons and folder badges

Choose the file icon set under **Options > Appearance > File icons**. File icon
sets are separate from **Application icon**, which controls the window, tray and
launcher artwork.

## Create a custom theme

Open **Options > Appearance > Icon themes folder**. The default location is
`~/.config/file-explorer/icons/`, or `$XDG_CONFIG_HOME/file-explorer/icons/` when
that variable is set. Create a subdirectory for your theme:

```text
icons/MyTheme/
  theme.conf
  folder.svg
  trash.svg
  folder-remote-smb.svg
  folder-remote-nfs.svg
  folder-publicshare.svg
  emblem-symbolic-link.svg
  emblem-git.svg
```

An optional `theme.conf` chooses the bundled fallback set:

```ini
base=Material
```

Other supported bases are `Catppuccin`, `Symbols`, and `Monokai`. Missing ordinary
icons come from the base set. SVG filenames such as `folder.svg`, `folder-src.svg`,
`trash.svg` and `python.svg` replace the corresponding artwork. `trash.svg` is the
Trash icon throughout the app when this theme is selected. `ext-<extension>.svg`
supplies an icon for any file extension. The generated `README.txt` in the icon themes
folder lists the built-in names and badge filenames.

Reopen **Icon themes folder** after editing files to reload the theme, then select
**MyTheme** under **File icons**. To use installed Reversal artwork, copy the
chosen SVGs into this custom-theme directory; editing the installed theme is not
required. Selecting Reversal directly uses its installed share and symlink icons.

## Badge filenames

| SVG filename | Meaning |
| --- | --- |
| `folder-remote-smb.svg` | Local Samba/SMB share, including Samba usershares |
| `folder-remote-nfs.svg` | Local NFS export |
| `folder-publicshare.svg` | WebDAV or another detected local share |
| `emblem-symbolic-link.svg` | Symbolic link overlay for folders and files |
| `emblem-git.svg` | Git repository folder overlay |

These SVGs are **small overlays**, not replacements for the base folder. Use a
transparent background and a square `viewBox`, with artwork that remains readable
at small sizes. `folder.svg` (or the folder's name-specific icon) remains visible
beneath the badges. Badge size is about 32% of the folder icon, with a minimum of
5 logical pixels.

A single badge sits at the lower right. Multiple badges stack upward along the
right edge without overlap: share at the bottom, then symbolic link, then Git.
If several sharing protocols apply to the same root, SMB takes precedence over
NFS, followed by WebDAV/other shares. Children of shared folders do not receive a
share badge unless they are also explicitly shared.

The Git repository badge defaults to the **bundled classic orange Git logo SVG**.
It is not loaded from the desktop theme's `emblem-git.svg` or
`emblem-vcs-normal.svg`. A custom file theme's `emblem-git.svg` overrides that
logo. Git status letters and change dots remain separate indicators; they are not
replaced by the repository badge.

## Fallbacks and installed themes

Installed desktop themes use `folder-remote-smb`, `folder-remote-nfs`,
`folder-publicshare`, and `emblem-symbolic-link`. Missing installed share artwork
falls back to the theme's ordinary folder icon. Missing custom share badges use
`folder-shared` from the custom theme or its bundled base set. Missing symlink
artwork uses a bundled arrow; missing Git artwork uses the classic Git logo.

An optional Nerd Font does not replace badge SVGs. Light navigation/sidebar
surfaces may darken pale icons while preserving their hue and shading; the source
SVGs are unchanged. Grid and preview artwork retain their normal rendering.

`net-<name>.svg` has a separate purpose: it adds artwork to the **Connect to
server > Icon** selector. It does not define a local folder's share badge.

See [local share detection](integration.md#local-shared-folders) for supported
configuration files and detection limits, and [settings](settings.md) for the
remaining appearance controls.
