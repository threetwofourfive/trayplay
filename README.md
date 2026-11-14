# trayplay

> **Virtual Microconsole in the System Tray**  
> A cross-platform, client-server microconsole architecture with a headless C++ core and 100% native system tray popovers.

---

## Architecture Overview

- **Core (`tray-console-daemon`)**: Headless C++ backend managing the cartridge game loop, audio and gamepad input via SDL2, IPC via POSIX Shared Memory & UNIX Domain Sockets, and frame rendering into Shared Memory.
- **Frontends**: Native thin clients per operating system:
  - **Linux**: GNOME (GJS/Clutter), KDE Plasma (QML), Cinnamon (CJS), XFCE (C++/GTK3/SNI + `libxfce4panel-2.0`), Waybar (C++).
  - **macOS**: Swift + SwiftUI (`NSStatusItem`, `NSPopover`).
  - **Windows**: C++/WinRT + `NotifyIcon` with native Mica styling.

For detailed design specs, see [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

---

## Prerequisites & Dependencies

All dependencies are provided by system packages (no bundled `vcpkg` or `third_party/` dependencies).

### Arch Linux / CachyOS / Manjaro

Install system development libraries from official repositories:

```bash
sudo pacman -S --needed base-devel cmake clang pkgconf sdl2 lua54 nlohmann-json miniz
```

Optional dependencies for the native XFCE frontend (`trayplay-xfce` + panel plugin):

```bash
sudo pacman -S --needed gtk3 libdbusmenu-glib gtk-layer-shell xfce4-panel
```

Install `sol2` from the AUR:

```bash
paru -S --needed sol2
# or: yay -S --needed sol2
```

*(Note: `lua54` is required because `sol2` supports Lua up to 5.4, whereas Arch's default `lua` package is 5.5. For building `.rpm` packages via CPack, `rpm-build` is also required.)*

### Ubuntu / Debian

```bash
sudo apt update && sudo apt install -y \
    build-essential \
    cmake \
    clang \
    git \
    pkg-config \
    libsdl2-dev \
    liblua5.4-dev \
    nlohmann-json3-dev \
    libminiz-dev \
    libsol2-dev
```

Optional dependencies for the native XFCE frontend:

```bash
sudo apt install -y libgtk-3-dev libdbusmenu-glib-dev libgtk-layer-shell-dev libxfce4panel-2.0-dev
```

### Fedora

```bash
sudo dnf install -y \
    gcc-c++ \
    clang \
    cmake \
    git \
    pkgconf-pkg-config \
    SDL2-devel \
    lua-devel \
    json-devel \
    miniz-devel \
    sol2-devel
```

Optional dependencies for the native XFCE frontend:

```bash
sudo dnf install -y gtk3-devel libdbusmenu-devel gtk-layer-shell-devel xfce4-panel-devel
```

*(For building `.rpm` packages via CPack, also install `rpm-build`.)*

---

## Building the Project

### Using Zed

1. Open the project in Zed.
2. In [CMakeLists.txt](CMakeLists.txt), click the triangle next to `project(TrayConsole ...)` and choose **`CMake: Configure Debug`** (or `Release`).
3. Click the triangle next to `add_executable(tray-console-daemon)` and select **`CMake: Build tray-console-daemon`**.
4. Run the daemon using **`CMake: Run tray-console-daemon`**.
5. Optional targets available via gutter triangles in [CMakeLists.txt](CMakeLists.txt):
   - `install-gnome-extension` — installs the GNOME Shell extension into `~/.local/share/gnome-shell/extensions/`.
   - `package-tar` — builds a portable `.tar.gz` archive via CPack.
   - `package-deb` — builds a `.deb` package via CPack.
   - `package-rpm` — builds an `.rpm` package via CPack (requires `rpmbuild`).

### Using the Command Line

```bash
# Configure (defaults to Release with -O3 + LTO; pass -DENABLE_NATIVE_ARCH=ON for local CPU -march=native)
cmake -B build -S .

# Build
cmake --build build --parallel

# Install GNOME Shell extension locally (optional, Linux/GNOME)
cmake --build build --target install-gnome-extension

# Run (GNOME / XFCE via unified launcher or direct frontend)
./build/trayplay-xfce          # XFCE system tray frontend (waits in tray; starts daemon on click)
./build/trayplay-xfce --show   # Start XFCE tray frontend and open the screen immediately
./build/tray-console-daemon    # Standalone core daemon
```

### CMake Options

| Option | Default | Description |
|---|---|---|
| `CMAKE_BUILD_TYPE` | `Release` | Build configuration (`Debug`, `Release`, `RelWithDebInfo`, `MinSizeRel`) |
| `BUILD_CORE` | `ON` | Build `tray-console-daemon` |
| `BUILD_FRONTEND_GNOME` | `ON` | Copy/install GNOME Shell extension |
| `BUILD_FRONTEND_XFCE` | `ON` | Build native XFCE tray client (`trayplay-xfce`) |
| `BUILD_FRONTEND_XFCE_PANEL` | `ON` | Build optional `XfcePanelPlugin` (`libtrayplay.so`) |
| `ENABLE_LTO` | `ON` | Enable Link-Time Optimization (IPO/LTO) on non-Debug builds |
| `ENABLE_NATIVE_ARCH` | `OFF` | Add `-march=native` (disable for portable `.deb`/`.rpm`/`.tar.gz` packages) |

---

## Controls & XFCE Usage Notes

- **Game Controls:** `W`/`A`/`S`/`D` (Latin & Cyrillic layouts) or Arrow keys (`up`/`left`/`down`/`right`), `Space` (`a`), `Enter` (`start`), `Esc` (`select` / close explorer / hide idle screen), `M` (`m` audio mute toggle).
- **Supported Cartridges:** `.gpak` bundles, standalone `.lua` scripts, and unpacked project folders containing `game.json`.
- **XFCE Tray vs Panel Plugin:** Launching `trayplay-xfce` (or `/usr/bin/trayplay` under XFCE) places the icon inside the existing XFCE Status Tray (`systray`) next to Telegram/Discord/Bluetooth without requiring panel edits, and removes both icon and daemon on **Quit**. When installed system-wide (`/usr/lib/xfce4/panel/plugins/libtrayplay.so`), **TrayPlay Microconsole** is also available in *Panel → Add New Items*.
- **Environment Overrides:** `TRAYPLAY_DAEMON` (explicit path to `tray-console-daemon`), `TRAYPLAY_GAMES_DIR` (default cartridge browser folder).

### Packaging

```bash
# Portable archive (.tar.gz)
cmake --build build --target package-tar

# Debian / Ubuntu package (.deb)
cmake --build build --target package-deb

# Fedora / RHEL package (.rpm, requires rpmbuild)
cmake --build build --target package-rpm
```
