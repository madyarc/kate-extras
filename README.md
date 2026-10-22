# kate-extras

A growing collection of plugins for KDE's [Kate](https://kate-editor.org/) editor.

They are native C++ plugins (Qt 6 / KDE Frameworks 6) and need no Python, no accounts and no API keys.

## The plugins

### Spotify
A side panel for controlling the Spotify desktop app without leaving Kate.

- Cover art, title, artist and album
- Play/pause, previous and next, a seek bar, and volume
- **Show Spotify** brings the Spotify window to the front; **Launch Spotify** starts it if it is closed
- **Tools → Spotify** menu actions that you can bind to keyboard shortcuts

It talks to the Spotify app over [MPRIS](https://specifications.freedesktop.org/mpris-spec/latest/) on the local D-Bus, so the Spotify desktop client has to be running. Some Spotify builds ignore volume changes from other apps; everything else works.

### Deadlines
A calendar side panel for keeping track of due dates.

- Days with open deadlines are highlighted, overdue ones are red, and today is underlined
- List views for **Upcoming and overdue** (next 14 days), **Selected day** or **All deadlines**
- Each deadline has a title, a date, an optional time and optional notes; tick it to mark it done
- Double-click a day to add a deadline there, or use **Tools → Deadlines → Add Deadline…**
- A summary line such as "2 overdue · 1 due today"

Deadlines are stored as plain JSON in `~/.local/share/katedeadlines/deadlines.json`, and every open Kate window stays in sync with that file.

### Resource Monitor
Small RAM and VRAM bars in the top-right corner of the menu bar, like the ones in Google Colab.

- Green below 70%, orange from 70–90%, red above 90%
- Hover for exact RAM, swap, VRAM and disk figures, plus the GPU's name, load and temperature
- NVIDIA GPUs are read through `nvidia-smi`, and AMD GPUs through sysfs
- **Battery friendly on hybrid laptops:** a powered-down discrete GPU is never queried, because querying it would wake it up

The meter lives in the menu bar, so it is hidden while the menu bar is hidden (Ctrl+M).

### TeX & Markdown Preview
A side panel that previews both Markdown and LaTeX.

- **`.md`**: live preview as you type, with math rendered by KaTeX (works offline)
- **`.tex`**: compiles with `latexmk` on save and shows the PDF in an embedded Okular view, with errors listed above it
- **SyncTeX both ways**: the PDF follows your cursor, and Shift+click in the PDF jumps back to the source line
- The Markdown preview also follows your cursor line

It has extra build dependencies (Qt WebEngine, KParts, KConfig and Okular), and the configure step downloads KaTeX and markdown-it from the npm registry once. See [texmdpreview/README.md](texmdpreview/README.md) for usage, shared math macros and offline builds.

## Requirements

- Kate built on KDE Frameworks 6 (Kate 24.02 or newer)
- CMake, extra-cmake-modules and a C++17 compiler
- The Qt 6 and KF6 development packages listed below

| Distribution | Install command |
|---|---|
| Arch Linux | `sudo pacman -S --needed base-devel cmake extra-cmake-modules qt6-base ktexteditor ki18n kcoreaddons kxmlgui` |
| Debian / Ubuntu | `sudo apt install build-essential cmake extra-cmake-modules qt6-base-dev libkf6texteditor-dev libkf6i18n-dev libkf6coreaddons-dev libkf6xmlgui-dev` |
| Fedora | `sudo dnf install gcc-c++ cmake extra-cmake-modules qt6-qtbase-devel kf6-ktexteditor-devel kf6-ki18n-devel kf6-kcoreaddons-devel kf6-kxmlgui-devel` |

Package names on Debian, Ubuntu and Fedora can differ between releases.

TeX & Markdown Preview needs a C++20 compiler and these packages as well. Compiling `.tex` files also needs `latexmk`.

| Distribution | Install command |
|---|---|
| Arch Linux | `sudo pacman -S --needed qt6-webengine kparts kconfig okular texlive-binextra` |
| Fedora | `sudo dnf install qt6-qtwebengine-devel kf6-kparts-devel kf6-kconfig-devel 'cmake(Okular6)' okular /usr/bin/latexmk` |

If you don't need it, configure with `-DBUILD_TEXMDPREVIEW=OFF`.

## Build and install

```sh
git clone https://github.com/vignesh-kumar-v/kate-extras.git
cd kate-extras
cmake -B build -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
sudo cmake --install build
```

To build only some of the plugins, switch the others off when configuring, for example `-DBUILD_SPOTIFY=OFF`. The options are `BUILD_SPOTIFY`, `BUILD_DEADLINES`, `BUILD_RESMON` and `BUILD_TEXMDPREVIEW`.

## Turning the plugins on

1. Restart Kate.
2. Open **Settings → Configure Kate… → Plugins**.
3. Tick **Spotify**, **Deadlines**, **Resource Monitor** and/or **TeX & Markdown Preview**, then click **OK**.

Kate remembers enabled plugins per session, so if you use several sessions, tick them in each one.

Spotify, Deadlines and TeX/MD Preview appear as tabs on the right sidebar, and the resource meter appears at the right end of the menu bar.

## Uninstall

```sh
sudo rm /usr/lib/qt6/plugins/kf6/ktexteditor/{katespotifyplugin,katedeadlinesplugin,kateresmonplugin,texmdpreview}.so
sudo rm -r /usr/share/texmdpreview
```

Some distributions use a different plugin path, such as `/usr/lib/x86_64-linux-gnu/qt6/plugins/...` on Debian and Ubuntu. `sudo xargs rm < build/install_manifest.txt` removes exactly what was installed.

Your deadlines stay in `~/.local/share/katedeadlines/` until you delete that folder.

## License

[MIT](LICENSE), except for the `texmdpreview/` folder, which is GPL-3.0-or-later (see [texmdpreview/LICENSE](texmdpreview/LICENSE)). A build that includes TeX & Markdown Preview is therefore distributed under the GPL; configure with `-DBUILD_TEXMDPREVIEW=OFF` for an MIT-only build.
