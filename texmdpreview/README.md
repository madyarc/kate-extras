# texmdpreview

A side panel for the [Kate](https://kate-editor.org) editor that previews both Markdown and LaTeX:

- **`.md`**: live preview as you type, with math rendered by KaTeX (works offline)
- **`.tex`**: compiles with `latexmk` on save and shows the PDF in an embedded Okular view, with errors listed above it
- **SyncTeX both ways**: the PDF follows your cursor, and Shift+click in the PDF jumps back to the source line
- **Markdown cursor sync**: the preview follows your cursor line

Requires KDE Plasma 6 (KDE Frameworks 6, Qt 6). Tested on Fedora 44 (KF 6.30, Qt 6.11).

## Install

### Arch Linux

```
git clone https://github.com/vignesh-kumar-v/kate-extras.git
cd kate-extras/texmdpreview/arch
makepkg -si
```

The `PKGBUILD` builds the checkout it sits in, so nothing is cloned twice. To update later: `git pull && makepkg -si`. For LaTeX compiling you also need `latexmk`, which is in `texlive-binextra`.

### Fedora

```
sudo dnf install cmake gcc-c++ extra-cmake-modules qt6-qtbase-devel qt6-qtwebengine-devel \
    kf6-ktexteditor-devel kf6-kparts-devel kf6-kcoreaddons-devel kf6-kconfig-devel \
    'cmake(Okular6)' /usr/bin/latexmk okular
git clone https://github.com/vignesh-kumar-v/kate-extras.git
cd kate-extras/texmdpreview
cmake -B build -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
sudo cmake --install build
```

The configure step downloads KaTeX and markdown-it from the npm registry once. To build offline, pass `-DKATEX_TARBALL=…` and `-DMDIT_TARBALL=…` pointing at the npm `.tgz` files.

### Enable it

Restart Kate, then go to **Settings → Configure Kate → Plugins** and tick **TeX & Markdown Preview**. The panel appears in the right sidebar as **TeX/MD Preview**.

You can untick Kate's built-in **Document Preview** plugin. It can't render math, and disabling it avoids a conflict over the embedded PDF viewer's settings.

## Usage

### Markdown

Supported math: `$...$`, `$$...$$`, `\(...\)`, `\[...\]`, and `\begin{align}...\end{align}` (also `equation`, `gather`, `multline`, and others, with or without `*`). Math inside code blocks and code spans is left alone. A `\newcommand` in a document applies to every formula after it.

**Shared macros (optional):** put one definition per line in `~/.config/texmdpreview/macros.tex`:

```
\newcommand{\R}{\mathbb{R}}
\DeclareMathOperator{\tr}{tr}
```

Lines KaTeX can't use (such as `\usepackage`) are skipped. Restart Kate after editing the file.

**Cursor sync:** the preview follows your cursor line, like the PDF does for LaTeX. If you scroll the preview away, **Ctrl+Alt+J** brings it back to the cursor line.

### LaTeX

Saving a `.tex` file compiles it. For multi-file projects, put these magic comments at the top of each sub-file:

```
% !TEX root = ../main.tex
% !TEX program = lualatex      (or xelatex; the default is pdflatex)
```

### SyncTeX

- **Source → PDF:** the PDF always follows your cursor line, and lands on it after each compile. If you scroll the PDF away, **Ctrl+Alt+J** brings it back to the cursor line.
- **PDF → source:** **Shift+click** in the PDF jumps to that line in Kate, opening the file if needed.

The embedded viewer keeps its own settings in `~/.config/texmdpreview-okularpartrc`, so it doesn't change the mouse mode of the standalone Okular app.

## Not yet

- Packages for Debian/Ubuntu and RPM-based distros

## Uninstall

- **Arch:** `sudo pacman -R texmdpreview`
- **Manual build:** `sudo xargs rm < build/install_manifest.txt`

## License

GPL-3.0-or-later. See [LICENSE](LICENSE).

At build time this project downloads [KaTeX](https://katex.org) and [markdown-it](https://github.com/markdown-it/markdown-it), both MIT-licensed.
