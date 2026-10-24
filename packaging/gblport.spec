Name:           gblport
Version:        %{_version}
Release:        1%{?dist}
Summary:        Native 64-bit port of MechWarrior 2: Ghost Bear's Legacy (DOS, 1995)
License:        Proprietary
BuildRequires:  gcc, make, SDL2-devel, mesa-libGL-devel
Requires:       SDL2

%description
Native port of MechWarrior 2: Ghost Bear's Legacy with an OpenGL renderer.
Requires your own copy of the original Ghost Bear's Legacy disc.

%prep
# source is already checked out in the working directory

%build
make -C %{_sourcedir} -j$(nproc) gbl

%install
install -Dm755 %{_sourcedir}/gbl %{buildroot}%{_bindir}/gbl
mkdir -p %{buildroot}%{_datadir}/gblport
%{_datadir}/applications/gblport.desktop
%{_datadir}/icons/hicolor/256x256/apps/gblport.png
cp -r %{_sourcedir}/assets %{buildroot}%{_datadir}/gblport/
install -Dm644 %{_sourcedir}/packaging/linux/gblport.desktop %{buildroot}%{_datadir}/applications/gblport.desktop
install -Dm644 %{_sourcedir}/packaging/linux/gblport.png %{buildroot}%{_datadir}/icons/hicolor/256x256/apps/gblport.png

%files
%{_bindir}/gbl
%{_datadir}/gblport
%{_datadir}/applications/gblport.desktop
%{_datadir}/icons/hicolor/256x256/apps/gblport.png
