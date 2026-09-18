# Building MeOS on Linux

The native Linux port is work in progress. At the moment every MeOS source except the application
frame (`meos.cpp`) builds and links on Linux against the Win32 compatibility layer
(`code/platform`, POSIX and Qt 6), together with a GUI workbench (`meos_gui_workbench`) and the
tests; the MeOS application itself, networking and hardware support follow in later stages. The packages below cover all stages,
so that they only need to be installed once.

## Tested environment

Linux Mint 22.3 (Ubuntu 24.04 "noble" package base), GCC 13.3, CMake 3.28.3, Ninja 1.11.1.
Other distributions have not been tested; package names refer to Ubuntu/Debian.

## Packages

```bash
sudo apt install build-essential cmake ninja-build pkg-config \
  qt6-base-dev qt6-multimedia-dev libxkbcommon-dev \
  libhpdf-dev libpng-dev zlib1g-dev libminizip-dev \
  libmariadb-dev libmariadb-dev-compat libcurl4-openssl-dev libasio-dev libssl-dev \
  fonts-dejavu-core fonts-liberation ttf-mscorefonts-installer
```

| Package | Needed for | Needed from | Verified version |
|---|---|---|---|
| `build-essential` | GCC and the C++ standard library | now | GCC 13.3.0 |
| `cmake` | Build system; at least 3.25 (presets version 6) | now | 3.28.3 |
| `ninja-build` | Generator used by all presets | now | 1.11.1 |
| `pkg-config` | Locating minizip and MariaDB | now | 1.8.1 |
| `qt6-base-dev` | GUI backend (Qt Widgets, Gui, Core), printing, clipboard, the `offscreen` platform plugin used by the tests | stage 1 | Qt 6.4.2 |
| `libxkbcommon-dev` | Silences `Could NOT find XKB` while configuring Qt | stage 1 | 1.6.0 |
| `libhpdf-dev` | PDF export (libharu) | now | 2.3.0 |
| `libpng-dev` | Images | now | 1.6.43 |
| `zlib1g-dev`, `libminizip-dev` | ZIP backups | now | zlib 1.3, minizip 1.3.0 |
| `libmariadb-dev`, `libmariadb-dev-compat` | MySQL/MariaDB server connection (linked now, used from stage 2) | now | client 3.3.17 (MariaDB 10.11.14 packages) |
| `libcurl4-openssl-dev` | HTTP(S) access (replaces WinInet) | stage 2 | 8.5.0 |
| `libasio-dev` | Prerequisite of RestBed (REST server), built into MeOS | now | asio 1.28.1 |
| `libssl-dev` | TLS for HTTP(S) access | stage 2 | OpenSSL 3.0.13 |
| `qt6-multimedia-dev` | Sound output | stage 3 | Qt 6.4.2 |
| `fonts-dejavu-core` | Lucida Console (see **Fonts**) | stage 1 | 2.37-8 |
| `ttf-mscorefonts-installer` | Arial, Times New Roman, Courier New (see **Fonts**) | stage 1 | 3.8.1ubuntu1 |
| `fonts-liberation` | Metric compatible stand-ins if the Microsoft fonts are not installed | stage 1 | 2.1.5-3 |

Notes:

- **RestBed** is not packaged. CMake downloads RestBed 4.6 (the release whose headers are in
  `code/restbed`) and the kashmir headers it needs from GitHub when configuring, checks their
  SHA-256 and builds RestBed against the system asio (`code/cmake/RestBed.cmake`). The first
  configure therefore needs network access. Offline, unpack the two archives named in that file and
  pass `-DFETCHCONTENT_SOURCE_DIR_MEOS_RESTBED=<dir> -DFETCHCONTENT_SOURCE_DIR_MEOS_KASHMIR=<dir>`.
  RestBed is built without its own SSL support; MeOS serves plain HTTP.
- `libmysqlclient-dev` can be used instead of the two MariaDB packages, but not together with them.
  CMake looks for the pkg-config module `libmariadb` first, then `mysqlclient`. The MariaDB client
  connects to MariaDB and MySQL servers. The code is compiled against the MySQL headers in
  `code/mysql`; the test `mysql_api_check` verifies that they match the header of the library found.
- When configuring a project that uses Qt, CMake may print `Could NOT find XKB`. The message is
  harmless; installing `libxkbcommon-dev` silences it.
- All versions in the table were checked on 2026-09-11 with a CMake project that finds and links
  every package; the font packages on 2026-09-17 with the workbench and `ctest`.

## Fonts

MeOS asks for the fonts of Windows by name. The window layer maps each name to the first family
installed from a list of its own, and takes the cell heights of the original, so that text keeps the
size and the line height it has on Windows (`code/platform/qt/win32_text.cpp`). Text widths are
measured against a Windows run by the test `gdi_metrics_compare`.

| Font MeOS asks for | Used for | On Linux | Package | Verified version |
|---|---|---|---|---|
| Segoe UI | the whole user interface (canvas text) | Selawik, embedded in the program | none, `code/platform/qt/fonts` (SIL Open Font License) | Selawik 1.01 |
| Lucida Console | `monoText`, fixed width lists | DejaVu Sans Mono, in the cell size of the original | `fonts-dejavu-core` | 2.37-8 |
| Arial, Times New Roman, Courier New | lists, reports and printing | the fonts themselves | `ttf-mscorefonts-installer` | 3.8.1ubuntu1 |
| MS Shell Dlg (`DEFAULT_GUI_FONT`, the font of the controls at scale 1) | buttons, input fields, lists | Microsoft Sans Serif and Tahoma are not packaged, so Arial is used | `ttf-mscorefonts-installer` | 3.8.1ubuntu1 |

Without `ttf-mscorefonts-installer` (it needs the acceptance of a licence) the metric compatible
Liberation fonts are used instead: Liberation Sans, Liberation Serif and Liberation Mono from
`fonts-liberation`. Text is then a few percent wider or narrower than on Windows in places.

Selawik is Microsoft's metric compatible open replacement for Segoe UI; it is embedded as a Qt
resource and needs no installation. It has no italic style and no hinting tables, so Qt slants it
itself and the layer hints its outlines vertically only. Segoe UI's own hinting cannot be
reproduced: text is 2.9 % wider or narrower than on Windows on average (measured over 1,882
values), and at some sizes the accent of a capital letter reaches one pixel above the cell.

On an unscaled screen the layer rounds text advances to whole pixels, as GDI does. On a scaled
screen (HiDPI), where Qt draws the glyphs at the size of the screen but would keep those advances,
it hints the outlines vertically only, so that the letters of a word stay together.

## System setup

- **Access to the SI card reader.** SPORTident master stations appear as `/dev/ttyUSB*`
  (Silicon Labs `cp210x` driver) or `/dev/ttyACM*`; no vendor driver is needed. The user needs to
  be in the `dialout` group:

  ```bash
  sudo usermod -aG dialout "$USER"   # log out and in again afterwards
  ```

  MeOS names serial ports the Windows way, so a COM number stands for a device by this fixed
  scheme (independent of what is plugged in, so a port chosen for a competition keeps its
  meaning):

  | Port in MeOS | Device | Typical use |
  |---|---|---|
  | COM1 – COM32 | `/dev/ttyUSB0` … | USB serial adapters: the SI master stations |
  | COM33 – COM64 | `/dev/ttyACM0` … | USB CDC devices |
  | COM65 – COM96 | `/dev/ttyS0` … | built-in UARTs |

  A device path such as `/dev/ttyUSB0` is accepted wherever a port name is asked for as well.

- **Locale.** String comparison and date/time formatting follow the user locale, as on Windows.
  `LANG` must name an installed UTF-8 locale (e.g. `de_DE.UTF-8`); `locale -a` lists the installed
  ones.

## Build and test

```bash
cmake --preset linux-debug   && cmake --build --preset linux-debug   && ctest --preset linux-debug
cmake --preset linux-release && cmake --build --preset linux-release && ctest --preset linux-release
```

The tests need no display: the window layer tests run with Qt's `offscreen` platform plugin, which
comes with `qt6-base-dev`.

## GUI workbench

`build/<preset>/code/meos_gui_workbench` shows real MeOS pages (`gdioutput`) without the MeOS
application frame: all text formats and colours, all controls with their callbacks, common dialogs,
and a competition in the runner table. It is a tool for checking the port, not part of a MeOS
installation.

```bash
cd <folder with sportident.cardsystem>   # from a MeOS installation; the runner table needs it
build/linux-debug/code/meos_gui_workbench [competition.meos] [-page 1|2|3]
```

`sportident.cardsystem` (the runner table needs it) and `clubnamemap.csv` (without it MeOS reports
an error at startup; the workbench writes an empty one into its data folder instead) come with the
Windows installer, not with the sources, and are not part of this repository. Keep a copy outside
the working tree and start the workbench there, for example in `../meos.private/install` next to
the checkout. Where a Linux installation gets these files is open, see `plans/linux-port.md`.

Without a file, page 3 shows a built-in demo competition. Settings are kept in
`~/.local/share/MeOS Workbench`.

### Comparing the pages with Windows

The workbench uses the Win32 API and `gdioutput` only, so it builds with MSVC as well: the target
`meos_gui_workbench` of the `win-debug` and `win-release` presets (the Windows side of the CMake
build has not been verified yet). `-shot <prefix>` shows every page in a canvas of a fixed size,
writes it and exits:

```bash
# writes linux-page1.bmp … linux-page3.csv
build/linux-debug/code/meos_gui_workbench -shot linux

# the same without a display
QT_QPA_PLATFORM=offscreen build/linux-debug/code/meos_gui_workbench -shot linux
```

- `<prefix>-page<N>.bmp` is what `gdioutput` draws, read back from the window with `BitBlt`. As on
  Windows, the child windows of the controls are not part of it, so page 2 shows its labels only,
  while pages 1 and 3 (the runner table is drawn by `gdioutput`) are complete. Windows needs the
  window to be visible for this, so nothing may cover it there.
- `<prefix>-page<N>.csv` holds the measures of the page and the position and size of every control
  in UTF-8, which is where differences in control heights show up. The files of two runs can be
  compared with `diff`, the images with an image viewer or `compare` (ImageMagick).

Both files are byte-identical between the Debug and the Release build and between runs, with and
without a display.

`code/platform/qt/workbench/windows-page1..3.csv` are the layout of a Windows run, so that a later
run can be compared without a Windows machine:

```bash
build/linux-debug/code/meos_gui_workbench -shot linux
for page in 1 2 3; do diff "code/platform/qt/workbench/windows-page$page.csv" "linux-page$page.csv"; done
```

They were taken on 2026-09-18 with the MSVC build of this workbench, the built-in demo competition
and the `sportident.cardsystem` of a MeOS installation. The differences to expect are the ones
stage 1.2.7 recorded (`plans/linux-port-1.2-qt-backend.md`): input fields 8 to 20 pixels narrower,
combo boxes 7 pixels higher, list boxes 10 pixels higher, buttons 1 to 2 pixels wider. Note that
the workbench reads `sportident.cardsystem` from the current folder, and that the numbers hold for
an unscaled screen; the workbench lays itself out at 96 dpi on both platforms.

**How far to take the comparison.** Pixel equality with Windows is not a goal; an intact layout is.
A few pixels more width or height on Linux are fine as long as nothing collides. Fix what breaks the
layout: text over other text or over a control, labels that are cut off or ellipsized where Windows
shows them in full, columns that no longer line up, controls that leave the page or the canvas, line
breaks in different places, a wrong scroll range, clipped ascenders, descenders or accents. Merely
record the rest: positions and sizes within ±2 px, a different page height, text widths within the
measured font tolerance (Arial, Times New Roman, Lucida Console ≤ 0.2 %, Segoe UI through Selawik
≤ 3 %), antialiasing, hinting, stroke weights and the glyph shapes of the substituted fonts. The CSV
decides, the BMP is a visual check and never a pass/fail gate.

Debug builds use AddressSanitizer and UBSan (switch off with `-DMEOS_SANITIZE=OFF`); Release builds
use `-O2 -g` with link-time optimization. Build output goes to `build/<preset>`.
