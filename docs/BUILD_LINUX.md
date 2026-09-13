# Building MeOS on Linux

The native Linux port is work in progress. At the moment the platform independent core
(`meos_core`), the Win32 compatibility layer (`meos_platform`), the first part of the Qt based
window layer (`meos_win32ui`) and their tests build on Linux; the GUI, networking and hardware
support follow in later stages. The packages below cover all stages,
so that they only need to be installed once.

## Tested environment

Linux Mint 22.3 (Ubuntu 24.04 "noble" package base), GCC 13.3, CMake 3.28.3, Ninja 1.11.1.
Other distributions have not been tested; package names refer to Ubuntu/Debian.

## Packages

```bash
sudo apt install build-essential cmake ninja-build pkg-config \
  qt6-base-dev qt6-multimedia-dev \
  libhpdf-dev libpng-dev zlib1g-dev libminizip-dev \
  libmariadb-dev libmariadb-dev-compat libcurl4-openssl-dev libasio-dev libssl-dev
```

| Package | Needed for | Needed from | Verified version |
|---|---|---|---|
| `build-essential` | GCC and the C++ standard library | now | GCC 13.3.0 |
| `cmake` | Build system; at least 3.25 (presets version 6) | now | 3.28.3 |
| `ninja-build` | Generator used by all presets | now | 1.11.1 |
| `pkg-config` | Locating minizip and MariaDB | now | 1.8.1 |
| `qt6-base-dev` | GUI backend, printing, clipboard | stage 1 | Qt 6.4.2 |
| `libhpdf-dev` | PDF export (libharu) | stage 1 | 2.3.0 |
| `libpng-dev` | Images | stage 1 | 1.6.43 |
| `zlib1g-dev`, `libminizip-dev` | ZIP backups | stage 1 | zlib 1.3, minizip 1.3.0 |
| `libmariadb-dev`, `libmariadb-dev-compat` | MySQL/MariaDB server connection; the compat package provides `mysql/mysql.h` as used by the code | stage 2 | client 3.3.17 |
| `libcurl4-openssl-dev` | HTTP(S) access (replaces WinInet) | stage 2 | 8.5.0 |
| `libasio-dev`, `libssl-dev` | Prerequisites of RestBed (REST server) | stage 2 | asio 1.28.1, OpenSSL 3.0.13 |
| `qt6-multimedia-dev` | Sound output | stage 3 | Qt 6.4.2 |

Notes:

- RestBed is not packaged. It will be built from source as part of the MeOS build in stage 2;
  only its prerequisites are listed above.
- `libmysqlclient-dev` can be used instead of the two MariaDB packages, but not together with them.
  The MariaDB client connects to MariaDB and MySQL servers.
- When configuring a project that uses Qt, CMake may print `Could NOT find XKB`. The message is
  harmless; installing `libxkbcommon-dev` silences it.
- All versions in the table were checked on 2026-09-11 with a CMake project that finds and links
  every package.
- **Fonts.** MeOS asks for Windows fonts. Arial, Times New Roman and Courier New come from
  `ttf-mscorefonts-installer` (optional; otherwise the metric compatible Liberation fonts are used).
  Segoe UI is replaced by Selawik, which is embedded in the build (`code/platform/qt/fonts`, SIL Open
  Font License); Lucida Console is drawn with DejaVu Sans Mono (`fonts-dejavu-core`, installed with
  Qt) in the cell size of the original.

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

The tests need no display: the window layer test runs with Qt's `offscreen` platform plugin, which
comes with `qt6-base-dev`.

Debug builds use AddressSanitizer and UBSan (switch off with `-DMEOS_SANITIZE=OFF`); Release builds
use `-O2 -g` with link-time optimization. Build output goes to `build/<preset>`.
