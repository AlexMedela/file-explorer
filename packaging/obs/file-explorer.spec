# File Explorer - RPM package for Fedora and openSUSE (built on OBS: home:AlexMedela/file-explorer, see _service).
Name:           file-explorer
Version:        0.9.0
Release:        0
Summary:        Just another file explorer: fast, keyboard-first, GPU-rendered
License:        MIT
URL:            https://github.com/AlexMedela/file-explorer
Source0:        %{name}-%{version}.tar.gz
BuildRequires:  gcc
BuildRequires:  grep
BuildRequires:  pkgconfig
BuildRequires:  pkgconfig(sdl3)
BuildRequires:  pkgconfig(freetype2)
BuildRequires:  pkgconfig(fontconfig)
BuildRequires:  pkgconfig(libarchive)
BuildRequires:  pkgconfig(vterm) >= 0.3
BuildRequires:  pkgconfig(fuse3)
%if 0%{?suse_version}
BuildRequires:  libopenssl-3-devel
%else
BuildRequires:  pkgconfig(openssl) >= 3.0
%endif
Recommends:     xdg-utils
Recommends:     polkit
Recommends:     udisks2
Suggests:       rclone

%description
A fast, keyboard-first file manager for Linux with a GPU-drawn interface: split panels with tabs, terminal tabs,
previews, search inside files, a disk usage map, folder compare, archives as folders, phones on a cable and Git
status. It never takes over your default file manager or file chooser unless you turn that on.

%prep
%autosetup -p1

%build
CFLAGS_PKG="%{optflags}" LDFLAGS_PKG="%{?build_ldflags}" ./build.sh package

%install
DESTDIR=%{buildroot} PREFIX=%{_prefix} ./install.sh

%files
%{_bindir}/file-explorer
%{_datadir}/applications/dev.fileexplorer.desktop
%{_datadir}/icons/hicolor/scalable/apps/dev.fileexplorer.svg
%dir %{_datadir}/polkit-1
%dir %{_datadir}/polkit-1/actions
%{_datadir}/polkit-1/actions/dev.fileexplorer.policy
%dir %{_prefix}/lib/udev
%dir %{_prefix}/lib/udev/rules.d
%{_prefix}/lib/udev/rules.d/70-file-explorer-phones.rules
%{_datadir}/doc/file-explorer/
%{_datadir}/licenses/file-explorer/

%changelog
