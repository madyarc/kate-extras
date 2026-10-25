# Build from a checkout with packaging/fedora/build-rpm.sh, or from a release
# tarball with: spectool -g -R kate-extras.spec && rpmbuild -ba kate-extras.spec

%global katex_version 0.16.22
%global mdit_version  14.1.0

Name:           kate-extras
Version:        1.0
Release:        1%{?dist}
Summary:        Plugins for Kate: Spotify, Deadlines, Resource Monitor, Jupyter Notebook and TeX & Markdown Preview

# texmdpreview/ is GPL-3.0-or-later, everything else is MIT;
# KaTeX and markdown-it are bundled and MIT
License:        MIT AND GPL-3.0-or-later
URL:            https://github.com/vignesh-kumar-v/kate-extras
Source0:        %{url}/archive/v%{version}/%{name}-%{version}.tar.gz
Source1:        https://registry.npmjs.org/katex/-/katex-%{katex_version}.tgz
Source2:        https://registry.npmjs.org/markdown-it/-/markdown-it-%{mdit_version}.tgz

BuildRequires:  gcc-c++
BuildRequires:  cmake
BuildRequires:  extra-cmake-modules
BuildRequires:  qt6-qtbase-devel
BuildRequires:  qt6-qtsvg-devel
BuildRequires:  qt6-qtwebengine-devel
BuildRequires:  kf6-ktexteditor-devel
BuildRequires:  kf6-ki18n-devel
BuildRequires:  kf6-kcoreaddons-devel
BuildRequires:  kf6-kxmlgui-devel
BuildRequires:  kf6-kwidgetsaddons-devel
BuildRequires:  kf6-kconfig-devel
BuildRequires:  kf6-kparts-devel
BuildRequires:  cmake(Okular6)

Requires:       kate
Requires:       okular
Recommends:     python3-ipykernel
Recommends:     /usr/bin/latexmk

Provides:       bundled(js-katex) = %{katex_version}
Provides:       bundled(js-markdown-it) = %{mdit_version}

%description
A collection of native plugins for KDE's Kate editor:

- Spotify: a side panel that controls the Spotify desktop app over MPRIS
- Deadlines: a calendar side panel for keeping track of due dates
- Resource Monitor: RAM and VRAM bars in the menu bar
- Jupyter Notebook: edit and run .ipynb files
- TeX & Markdown Preview: live Markdown preview with KaTeX math, and LaTeX
  to PDF preview with SyncTeX

%prep
%autosetup -p1

%build
%cmake \
    -DKATEX_VERSION=%{katex_version} \
    -DMDIT_VERSION=%{mdit_version} \
    -DKATEX_TARBALL=%{SOURCE1} \
    -DMDIT_TARBALL=%{SOURCE2}
%cmake_build

%install
%cmake_install

%files
%license LICENSE
%license texmdpreview/LICENSE
%doc README.md
%{_libdir}/qt6/plugins/kf6/ktexteditor/katespotifyplugin.so
%{_libdir}/qt6/plugins/kf6/ktexteditor/katedeadlinesplugin.so
%{_libdir}/qt6/plugins/kf6/ktexteditor/kateresmonplugin.so
%{_libdir}/qt6/plugins/kf6/ktexteditor/katenotebookplugin.so
%{_libdir}/qt6/plugins/kf6/ktexteditor/texmdpreview.so
%{_datadir}/texmdpreview/

%changelog
* Sun Oct 25 2026 Madhesh <https://github.com/madyarc> - 1.0-1
- First package
