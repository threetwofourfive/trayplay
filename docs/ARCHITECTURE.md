# Virtual Microconsole in the System Tray

## Architecture
Due to Wayland protocol limitations, it is impossible to create a single cross-shell application that behaves as a native popover from the system tray across all desktop environments (GNOME, KDE, etc.). 
Therefore, the project is divided into two parts: **Core** (server side) and **Frontends** (client side).

### Core (Backend)
A background daemon written in C++.
- Runs the game loop.
- Loads files (e.g., `.gpak`).
- Renders frames.
- Sends rendered frames via Shared Memory or UNIX-sockets.
- Receives control commands via D-Bus.
- Has no graphical user interface (GUI).

### Frontends (Clients)
Thin clients tailored to specific desktop environments. They are displayed as an icon in the system tray.
- Receive frames from the Core.
- Draw frames in the native popover windows of their respective desktop environments.
- Send commands to the Core (e.g., via D-Bus).

## User Interaction (UI)
- **LMB (Left Mouse Button) on the tray icon:** Opens a native popover. Inside is a black screen (canvas) for displaying graphics from the Core, along with a "Load" button.
- **RMB (Right Mouse Button) on the tray icon:** Opens a standard context menu with an "Exit" option (closes the application).

## Tech Stack for Frontends
- **GNOME:** GJS (JavaScript) + Clutter
- **KDE Plasma:** QML + JavaScript (or C++ Qt plugin)
- **Cinnamon:** CJS (JavaScript)
- **XFCE:** C/C++ + GTK3/GTK4
- **Waybar:** C++
