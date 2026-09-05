# Virtual Microconsole in the System Tray

## Architecture
To achieve a 100% native look and feel on every operating system without relying on non-native cross-platform UI frameworks (like Qt), the project uses a strictly Client-Server architecture. 
The project is divided into a single cross-platform **Core** (server side) and multiple OS-specific **Frontends** (client side).

### Core (Backend)
A headless cross-platform daemon written in C++.
- **Game Logic:** Uses a WebAssembly engine (e.g., Wasmtime) or Lua to safely execute cartridge logic (`.gpak`).
- **Media & Input:** Uses SDL2 (or SDL3) directly for audio output and gamepad input.
- **Rendering:** Renders frames in the background.
- **IPC (Inter-Process Communication):** Uses Boost (Asio + Interprocess) to provide a unified abstraction over OS-specific IPC mechanisms:
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
- **XFCE:** C/C++ + GTK3/GTK4
- **Waybar:** C++

### macOS
- **Stack:** Swift + SwiftUI
- **Details:** Uses `NSStatusItem` for the menu bar item and an `NSPopover` hosting native SwiftUI components (`NSHostingController`) with modern materials and SF Symbols.

### Windows
- **Stack:** C++/WinRT (or C# WinUI 3 / WPF)
- **Details:** Uses `NotifyIcon` for the system tray and modern windows with Mica material for the UI.
