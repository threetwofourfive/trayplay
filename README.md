# trayplay

> **Virtual Microconsole in the System Tray**  
> A cross-platform, client-server microconsole architecture with a headless C++ core and 100% native system tray popovers.

---

## Architecture Overview

- **Core (`tray-console-daemon`)**: Headless C++ backend managing the cartridge game loop, audio and gamepad input via SDL2, IPC via Boost (Asio + Interprocess), and frame rendering into Shared Memory.
- **Frontends**: Native thin clients per operating system:
  - **Linux**: GNOME (GJS/Clutter), KDE Plasma (QML), Cinnamon (CJS), XFCE (GTK3/4), Waybar (C++).
  - **macOS**: Objective-C++ + AppKit (`NSStatusItem`, `NSPopover`).
  - **Windows**: C++/WinRT + `NotifyIcon` with native Mica styling.

For detailed design specs, see [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

---

## Editor & Development Setup: VS Code

This project is designed to be developed entirely within **VS Code** across all supported platforms (Linux, macOS, and Windows).

### Recommended VS Code Extensions
- **[C/C++](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cpptools)** (Microsoft)
- **[CMake Tools](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cmake-tools)** (Microsoft)

With these extensions installed, VS Code automatically detects `CMakeLists.txt` and the bundled `vcpkg` toolchain.

---

## Prerequisites by Operating System

We use lightweight command-line toolchains and package managers across all platforms:

### Linux (Ubuntu / Debian)

Install the compiler toolchain, Autotools (required for vcpkg port builds), and development headers:

```bash
sudo apt update && sudo apt install -y \
    build-essential \
    cmake \
    git \
    curl \
    zip \
    unzip \
    tar \
    pkg-config \
    autoconf \
    autoconf-archive \
    automake \
    libtool \
    libx11-dev \
    libxft-dev \
    libxext-dev \
    libwayland-dev \
    libxkbcommon-dev \
    libegl1-mesa-dev
```

### macOS

Install build tools and CMake via Homebrew (just like on Linux):

```bash
brew install cmake git pkg-config
```

### Windows (MinGW-w64)

Install the GCC/G++ MinGW toolchain, CMake, and Git:

- **Via MSYS2 (UCRT64)**:
  ```bash
  pacman -S --needed base-devel mingw-w64-ucrt-x86_64-toolchain mingw-w64-ucrt-x86_64-cmake git
  ```
- **Or via Scoop**:
  ```powershell
  scoop install mingw cmake git
  ```
- **Or via [WinLibs](https://winlibs.com/)**: Download the standalone UCRT package and add its `bin/` directory to your system `PATH`.

---

## Getting Started

### 1. Clone the Repository

Clone with submodules so that the bundled `vcpkg` dependency manager is initialized:

```bash
git clone --recursive https://github.com/threetwofourfive/trayplay.git
cd trayplay
```

> *If you already cloned without `--recursive`, run:*
> ```bash
> git submodule update --init --recursive
> ```

### 2. Bootstrap `vcpkg` (One-time step)

Build the local `vcpkg` executable:

- **Linux & macOS:**
  ```bash
  ./vcpkg/bootstrap-vcpkg.sh
  ```
- **Windows (PowerShell or Command Prompt):**
  ```cmd
  .\vcpkg\bootstrap-vcpkg.bat
  ```

---

## Building the Project

### Using VS Code

1. Open the project in VS Code:
   ```bash
   code .
   ```
2. Select your compiler kit when prompted by CMake Tools:
   - **Linux**: GCC or Clang
   - **macOS**: GCC or Clang
   - **Windows**: MinGW GCC
3. Press `F7` (or click **Build** on the bottom status bar). The bundled `vcpkg` toolchain is detected automatically.

---

### Using the Command Line

The root `CMakeLists.txt` automatically detects and uses the bundled `vcpkg` toolchain file (`vcpkg/scripts/buildsystems/vcpkg.cmake`).

#### Linux & macOS

```bash
# Configure (bundled vcpkg toolchain is auto-detected)
cmake -B build -S .

# Build
cmake --build build
```

#### Windows (with MinGW)

```cmd
# Configure with MinGW Makefiles or Ninja
cmake -B build -S . -G "MinGW Makefiles"

# Build
cmake --build build
```

---

## Troubleshooting & FAQ

### 1. Missing Autotools on Linux (`libxcrypt` or `vcpkg-make` failure)
- **Symptom:** `vcpkg` fails with `libxcrypt currently requires autoconf autoconf-archive automake libtoolize`.
- **Fix:** Run:
  ```bash
  sudo apt install -y autoconf autoconf-archive automake libtool
  ```

### 2. Python virtual environment error (`libsystemd` / `ensurepip`)
- **Symptom:** `The virtual environment was not created successfully because ensurepip is not available`.
- **Reason:** SDL2 by default enables D-Bus on Linux, which pulls in `libsystemd` and requires `python3-venv` from source.
- **Fix:** In `vcpkg.json`, `sdl2` is configured with `"default-features": false` and explicit `["x11", "wayland"]` features. This ensures the headless daemon does not compile unnecessary desktop service daemons.

### 3. Stalled or interrupted downloads
- If an archive download stalls without a timeout, you can cancel the process (`Ctrl+C`), download the required tarball directly into `vcpkg/downloads/`, and re-run the build command. `vcpkg` will verify the file hash and continue.