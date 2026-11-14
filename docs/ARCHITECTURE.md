# Virtual Microconsole in the System Tray

## Architecture
To achieve a 100% native look and feel on every operating system without relying on non-native cross-platform UI frameworks (like Qt), the project uses a strictly Client-Server architecture. 
The project is divided into a single cross-platform **Core** (server side) and multiple OS-specific **Frontends** (client side).

### Core (Backend)
A headless cross-platform daemon written in C++.
- **Game Logic:** Uses Lua 5.4 with Sol2 bindings to safely execute cartridge logic (`.gpak`).
- **Media & Input:** Uses SDL2 directly for audio output and gamepad input.
- **Rendering:** Renders frames in the background.
- **IPC (Inter-Process Communication):** Uses native OS-specific IPC primitives (no external IPC library dependencies):
  - **Windows:** Named Pipes for commands, File Mapping for sharing rendered video frames.
  - **UNIX (Linux/macOS):** Domain sockets for commands, Shared Memory (`shm`) for video frames.

### Frontends (Clients)
Thin, 100% native clients tailored to specific operating systems and desktop environments. They reside in the system tray, listen to the Shared Memory, and draw frames.
- **LMB (Left Mouse Button) on the tray icon:** Opens a native popover (with correct OS-specific blurring, shadows, and animations). Inside is a canvas displaying graphics from the Core.
- **RMB (Right Mouse Button) on the tray icon:** Opens a standard native context menu with an "Exit" option.

## Tech Stack for Frontends

### Linux
- **GNOME:** GJS (JavaScript) + Clutter
- **KDE Plasma:** QML + JavaScript (or C++ Qt plugin)
- **Cinnamon:** CJS (JavaScript)
- **XFCE:** C++17 + GTK3 + Cairo (`StatusNotifierItem` / `DBusMenu` tray client + optional `libxfce4panel-2.0` plugin)
- **Waybar:** C++

### macOS
- **Stack:** Swift + SwiftUI
- **Details:** Uses `NSStatusItem` for the menu bar item and an `NSPopover` hosting native SwiftUI components (`NSHostingController`) with modern materials and SF Symbols.

### Windows
- **Stack:** C++/WinRT (or C# WinUI 3 / WPF)
- **Details:** Uses `NotifyIcon` for the system tray and modern windows with Mica material for the UI.

---

## Build & Packaging Notes (Linux)

- **Compiler & Dependencies:** `CMakeLists.txt` prefers `clang++` if available, defaults to `Release` (`-O3` + optional `ENABLE_LTO=ON`, and optional `ENABLE_NATIVE_ARCH=OFF` for `-march=native`) when `CMAKE_BUILD_TYPE` is unset, and resolves all dependencies (`SDL2`, `Lua 5.4`, `sol2`, `nlohmann_json`, `miniz`, plus optional `GTK3` / `dbusmenu-glib-0.4` / `gtk-layer-shell-0` / `libxfce4panel-2.0`) from system packages (no bundled `vcpkg` or `third_party/`).
- **On-Demand Daemon Lifecycle:** Thin clients (`trayplay` / `trayplay-xfce`) reside quietly in the panel/tray on launch and spawn `tray-console-daemon` on demand when the console popover is opened, shutting the daemon down cleanly on `Quit`.
- **Portable `.tar.gz` (`package-tar`):** Built via `cpack -G TGZ` without external packaging tools.
- **Debian `.deb` (`package-deb`):** Uses `cpack -G DEB` with `CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON`. When built on non-Debian distributions (e.g. Arch/CachyOS) without `dpkg` in `PATH`, CPack emits a warning, falls back to the `i386` architecture label, and omits shared-library `Depends` metadata. Release `.deb` packages should be built on Debian/Ubuntu or in an environment with `dpkg` installed.
- **Fedora/RHEL `.rpm` (`package-rpm`):** Uses `cpack -G RPM` and requires `rpmbuild` (`rpm-build`).

---

## Detailed Subsystem Documentation

For deep technical breakdowns of each subsystem:
- [Core Engine Architecture & Technical Specification](CORE_ENGINE.md)
- [GNOME Shell Frontend Architecture & Implementation](GNOME_FRONTEND.md)
- [XFCE Frontend Architecture & Implementation](XFCE_FRONTEND.md)
