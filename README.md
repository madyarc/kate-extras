# kate-extras

A growing collection of plugins for KDE's [Kate](https://kate-editor.org/) editor.

They are native C++ plugins (Qt 6 / KDE Frameworks 6) and need no Python, no accounts and no API keys. Spotify, Deadlines and Resource Monitor run on Linux and macOS ([macOS instructions](#macos)).

## The plugins

### Spotify
A side panel for controlling the Spotify desktop app without leaving Kate.

- Cover art, title, artist and album
- Play/pause, previous and next, a seek bar, and volume
- **Show Spotify** brings the Spotify window to the front; **Launch Spotify** starts it if it is closed
- **Tools → Spotify** menu actions that you can bind to keyboard shortcuts

On Linux it talks to the Spotify app over [MPRIS](https://specifications.freedesktop.org/mpris-spec/latest/) on the local D-Bus, so the Spotify desktop client has to be running. Some Spotify builds ignore volume changes from other apps; everything else works.

On macOS it uses Spotify's AppleScript interface instead. The first time, macOS asks whether Kate may control Spotify; if you said no, turn it back on under System Settings → Privacy & Security → Automation.

### Deadlines
A calendar side panel for keeping track of due dates.

- Days with open deadlines are highlighted, overdue ones are red, and today is underlined
- List views for **Upcoming and overdue** (next 14 days), **Selected day** or **All deadlines**
- Each deadline has a title, a date, an optional time and optional notes; tick it to mark it done
- Double-click a day to add a deadline there, or use **Tools → Deadlines → Add Deadline…**
- A summary line such as "2 overdue · 1 due today"

Deadlines are stored as plain JSON in `~/.local/share/katedeadlines/deadlines.json` (on macOS, `~/Library/Application Support/katedeadlines/deadlines.json`), and every open Kate window stays in sync with that file.

### Resource Monitor
Small RAM and VRAM bars in the top-right corner of the menu bar, like the ones in Google Colab.

- Green below 70%, orange from 70–90%, red above 90%
- Hover for exact RAM, swap, VRAM and disk figures, plus the GPU's name, load and temperature
- NVIDIA GPUs are read through `nvidia-smi`, and AMD GPUs through sysfs
- **Battery friendly on hybrid laptops:** a powered-down discrete GPU is never queried, because querying it would wake it up

The meter lives in the menu bar, so it is hidden while the menu bar is hidden (Ctrl+M).

On macOS the window has no menu bar of its own, so the meter sits among the menu bar extras at the top right of the screen as **Memory** with a usage bar (the same "Memory Used" figure as Activity Monitor). Click it for Memory, Swap, Storage and the GPU's current load. Macs have no separate VRAM to show: Apple silicon GPUs share system memory. Linux desktops with a global menu get the RAM / VRAM meter as a system tray icon instead.

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
2. Open **Settings → Configure Kate… → Plugins**. On macOS it is **Kate → Preferences…** (⌘,) in the menu bar instead.
3. Tick **Spotify**, **Deadlines**, **Resource Monitor** and/or **TeX & Markdown Preview**, then click **OK**.

Kate remembers enabled plugins per session, so if you use several sessions, tick them in each one.

Spotify, Deadlines and TeX/MD Preview appear as tabs on the right sidebar, and the resource meter appears at the right end of the menu bar.

## macOS

Kate for macOS ([download](https://kate-editor.org/get-it/)) ships its own copy of Qt and KDE Frameworks inside `kate.app`, and a plugin has to be built against exactly those versions. `macos/install.sh` takes care of that:

```sh
brew install cmake ninja sevenzip
git clone https://github.com/vignesh-kumar-v/kate-extras.git
cd kate-extras
macos/install.sh
```

The script:

1. reads the Qt and Frameworks versions out of `kate.app` and downloads matching development files from KDE's [Craft](https://community.kde.org/Craft) binary cache into `build-sdk/` (a 95 MB download that takes 360 MB on disk, only the first time; see `macos/setup-sdk.sh`),
2. builds Spotify, Deadlines and Resource Monitor and links them to the libraries inside `kate.app`,
3. copies them to `kate.app/Contents/PlugIns/kf6/ktexteditor/`,
4. **re-signs `kate.app` on your Mac (ad hoc)**. KDE signs Kate with the hardened runtime, which refuses any plugin that KDE did not sign itself, so this step is unavoidable. It also lets macOS ask for permission to control Spotify, and clears the download quarantine flag so Gatekeeper does not reject the re-signed app.

Then restart Kate and turn the plugins on as described above. If macOS says your terminal was prevented from modifying apps, allow it under System Settings → Privacy & Security → App Management and run the script again.

A Kate update replaces `kate.app`, which removes the plugins and restores KDE's signature: run `macos/install.sh` again afterwards. If the new Kate uses newer Qt or Frameworks versions, delete `build-sdk/` first so the script fetches matching ones. Kate is looked for in `/Applications/kate.app`; set `KATE_APP` to use another location.

`macos/install.sh --uninstall` removes the plugins again. To get KDE's original signature back, reinstall Kate.

TeX & Markdown Preview is not built on macOS, because Kate's macOS bundle does not include Okular.

## Uninstall

On macOS, see [above](#macos). On Linux:

```sh
sudo rm /usr/lib/qt6/plugins/kf6/ktexteditor/{katespotifyplugin,katedeadlinesplugin,kateresmonplugin,texmdpreview}.so
sudo rm -r /usr/share/texmdpreview
```

Some distributions use a different plugin path, such as `/usr/lib/x86_64-linux-gnu/qt6/plugins/...` on Debian and Ubuntu. `sudo xargs rm < build/install_manifest.txt` removes exactly what was installed.

Your deadlines stay in `~/.local/share/katedeadlines/` (macOS: `~/Library/Application Support/katedeadlines/`) until you delete that folder.

## License

[MIT](LICENSE), except for the `texmdpreview/` folder, which is GPL-3.0-or-later (see [texmdpreview/LICENSE](texmdpreview/LICENSE)). A build that includes TeX & Markdown Preview is therefore distributed under the GPL; configure with `-DBUILD_TEXMDPREVIEW=OFF` for an MIT-only build.
