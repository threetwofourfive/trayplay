# XFCE Frontend Architecture & Implementation

This document provides a comprehensive technical breakdown of the native XFCE frontend for **TrayPlay**, detailing its hybrid architecture, system tray integration via `StatusNotifierItem` and `DBusMenu`, borderless GTK3 popover positioning on X11 and Wayland, zero-copy Shared Memory rendering, and optional `XfcePanelPlugin` wrapper.

---

## 1. Architectural Motivation & Comparison with GNOME

Unlike GNOME Shell on Wayland—where external clients cannot place arbitrary widgets in the top bar or spawn panel popovers without running inside the compositor process as a GNOME Shell extension—the **XFCE Desktop Environment** provides native, modular panel and tray infrastructure:

1. **System Tray (`xfce4-panel` `systray` / Status Tray Plugin):** Applications register dynamically via the Freedesktop/KDE **`org.kde.StatusNotifierItem` (SNI)** D-Bus protocol and appear alongside existing tray applications (such as Telegram, Discord, and Blueman) without requiring manual panel editing or session re-login.
2. **On-Demand Tray Lifecycle:**
   - Launching **TrayPlay** (`/usr/bin/trayplay` -> `/usr/bin/trayplay-xfce`) starts a lightweight single-instance `GtkApplication` that places the TrayPlay icon in the system tray and waits quietly for a click (`tray-console-daemon` is not spawned until the user opens the screen).
   - Clicking **Quit** (either in the popup header or the tray right-click menu) gracefully shuts down `tray-console-daemon` and exits `trayplay-xfce`, immediately removing the icon from the tray.
3. **GTK 3 Toolkit Alignment:** Although [ARCHITECTURE.md](ARCHITECTURE.md) initially noted `GTK3/GTK4`, XFCE 4.20 (`libxfce4panel-2.0` and `libxfce4ui-2`) is built strictly on **GTK 3 (`gtk+-3.0`)**. Using **C++17 + GTK 3 + Cairo** guarantees 100% visual fidelity with the active XFCE GTK theme and enables sharing the exact same UI widget across both the tray client and the native `XfcePanelPlugin` module.

---

## 2. Directory Structure & Hybrid Modular Design

The XFCE frontend resides in `frontends/xfce/`:

```text
frontends/xfce/
├── ipc_client.h / .cpp        # Inode-aware POSIX SHM reader, async UNIX socket client, daemon launcher
├── settings.h / .cpp          # Persistent config (~/.config/trayplay/settings.json) shared with GNOME
├── cartridge_browser.h / .cpp # Embedded GTK3 cartridge explorer & GtkFileChooserNative picker
├── trayplay_view.h / .cpp     # Reusable microconsole GTK3 widget (Header, 320x180 Cairo screen, Controls)
├── popup_window.h / .cpp      # Borderless GTK3 popover window (X11 workarea & Wayland layer-shell)
├── status_notifier.h / .cpp   # Native GDBus StatusNotifierItem + DbusmenuServer + GtkStatusIcon fallback
├── main_app.cpp               # Single-instance tray application (`trayplay-xfce`)
├── panel_plugin.cpp           # Optional native XfcePanelPlugin (`libtrayplay.so` via libxfce4panel-2.0)
├── trayplay.desktop.in        # XFCE panel plugin desktop descriptor (`Type=X-XFCE-PanelPlugin`)
├── style.css                  # GTK3 CSS stylesheet blending XFCE theme tokens with CRT console styling
└── generated_style.h.in       # CMake template embedding style.css into the binary at configure time
```

### Shared Core + Two Entry Points

All UI rendering, SHM frame polling, IPC communication, cartridge browsing, and keyboard input live in a single PIC static library (`trayplay_xfce_core`), powering two native entry points:

1. **`trayplay-xfce` (Primary Tray Client):** Launched by `/usr/bin/trayplay` and `trayplay.desktop`. Appears in `systray` right beside other tray apps on launch and disappears cleanly on `Quit`. Works both from system packages (`/usr/bin/trayplay-xfce`) and directly from `./build/trayplay-xfce` during development.
2. **`libtrayplay.so` (Optional `XfcePanelPlugin`):** Built automatically when `libxfce4panel-2.0` is available (`BUILD_FRONTEND_XFCE_PANEL=ON`) and installed into the distro's `xfce4/panel/plugins` directory with `X-XFCE-Internal=false` (running isolated inside `wrapper-2.0`). Useful for users who disable `systray` or want TrayPlay pinned permanently via *Panel -> Add New Items*.

---

## 3. Key Technical Challenges & Engineering Solutions

### 3.1. `StatusNotifierItem` + `DBusMenu` vs `libayatana-appindicator3`

#### Problem
Standard GTK tray libraries have critical limitations for a microconsole popover:
- **`libayatana-appindicator3`** forces left-clicks to display a static `GtkMenu`. It cannot trigger a custom interactive 60 FPS canvas popover or capture keyboard game input.
- **`GtkStatusIcon`** is deprecated in GTK 3, restricted to X11 XEmbed (invisible on Wayland), and classified as a legacy tray icon in XFCE.
- Inspection of XFCE 4.20's `libsystray.so` confirms that XFCE's Status Tray Plugin handles left-clicks via `org.kde.StatusNotifierItem.Activate(x, y)` only when `ItemIsMenu = false`, and constructs right-click context menus exclusively via `com.canonical.dbusmenu` (`dbusmenu_gtkmenu_new`), without calling `ContextMenu()`.

#### Solution
`StatusNotifier` implements the **`org.kde.StatusNotifierItem`** specification directly over **`GDBus`** alongside **`DbusmenuServer`** (`libdbusmenu-glib`):
- Exports `ItemIsMenu = false` so **LMB** invokes `Activate(x, y)` with screen coordinates to toggle the game popover.
- Exports `Menu = "/MenuBar"` served by `DbusmenuServer`, so **RMB** opens a native XFCE panel context menu (`TrayPlay Microconsole`, `Open Screen` / `Hide Screen`, `Eject Cartridge`, `Quit`).
- Watches `org.kde.StatusNotifierWatcher` (`g_bus_watch_name_on_connection`) to automatically re-register if `xfce4-panel` restarts, and falls back gracefully to `GtkStatusIcon` if no `StatusNotifierHost` is active on the session bus.

---

### 3.2. Borderless `GtkWindow` Popovers vs `GtkPopover`

#### Problem
In GTK 3 on X11, a `GtkPopover` is constrained within the geometry of its parent toplevel `GtkWindow`. Because `xfce4-panel` is typically ~27 px tall (and a `StatusNotifierItem` process lives outside the panel process), `GtkPopover` cannot extend outside the panel bounds.

#### Solution
Following the same pattern used by native XFCE plugins (`xfce4-pulseaudio-plugin`, `xfce4-clipman-plugin`), `PopupWindow` uses an undecorated `GtkWindow` (`GTK_WINDOW_TOPLEVEL`) configured with:
- `GDK_WINDOW_TYPE_HINT_POPUP_MENU`, `skip-taskbar`, `skip-pager`, `keep-above`, and `stick`.
- Composited RGBA visual (`gdk_screen_get_rgba_visual` + `CAIRO_OPERATOR_SOURCE` clear) for crisp rounded borders, with automatic fallback (`.non-composited`) when compositing is disabled.
#### Solution
- **X11 Positioning:** Uses `(x, y)` from `Activate(x, y)` combined with `GdkMonitor` geometry and `workarea` (`gdk_monitor_get_geometry` / `gdk_monitor_get_workarea`) to detect whether the panel sits at the top, bottom, left, or right edge of the active monitor, aligning and clamping the popup cleanly without overlapping the panel (and positioning at the matching monitor edge when opened via menu prior to a tray click).
- **Wayland Positioning:** Uses `gtk-layer-shell` (`GTK_LAYER_SHELL_LAYER_OVERLAY`, `GTK_LAYER_SHELL_KEYBOARD_MODE_ON_DEMAND`) for `trayplay-xfce`, and `xfce_panel_plugin_popup_window()` (when `libxfce4panel >= 4.19.0`) inside `panel_plugin.cpp`.

---

### 3.3. Focus-Out vs Tray Icon Toggle Race & Modal Dialogs

#### Problem
On X11, when the popup window is open and the user clicks the tray icon (or panel button) to close it:
1. Pressing the mouse button on the panel causes the window manager to shift focus away from `PopupWindow`, triggering `focus-out-event` -> `hide()`.
2. Milliseconds later, the button release dispatches `Activate(x, y)` over D-Bus (or `toggled` on the panel button). Seeing `!is_visible()`, a naive toggle immediately re-opens the popup.

Additionally, opening the native system file chooser (`GtkFileChooserNative`) steals focus from `PopupWindow` and would prematurely dismiss it.

#### Solution
1. `PopupWindow` records a monotonic timestamp (`m_last_focus_hide_us`) whenever `focus-out-event` hides the window and ignores `toggle_near()` / toggle reopen within a **250 ms** debounce window.
2. `CartridgeBrowser` notifies `PopupWindow::set_modal_dialog_active(true)` while `GtkFileChooserNative` is open, suspending focus-out dismissal and re-presenting the popup once a cartridge is chosen or cancelled.

---

### 3.4. Zero-Copy SHM Inode Tracking & Direct Cairo Surface Rendering

#### Problem
If `tray-console-daemon` exits and restarts (`shm_unlink` + `shm_open`), a frontend holding an already-mapped file descriptor continues reading the unlinked old shared memory inode. Furthermore, allocating a new `GdkPixbuf` on every frame at 60 FPS adds unnecessary heap churn.

#### Solution
1. **`stat()` Inode Verification with Syscall Throttling:** When `m_shm_ptr` is already mapped and streaming smoothly, `IpcClient::ensure_shm_mapped()` throttles `::stat("/dev/shm/trayplay_framebuffer")` to once every 30 poll ticks (~0.5 s) rather than hitting the filesystem 60 times per second, while still automatically closing and re-`mmap`ping whenever `st_ino`, `st_dev`, or `st_size` changes across daemon restarts (and checking immediately when disconnected).
2. **SIMD RGBA32 -> ARGB32 Conversion & Lock-Free Tear Protection:** `TrayPlayView` keeps a single 320×180 `CAIRO_FORMAT_ARGB32` `cairo_surface_t`. On little-endian x86_64, `rgba_to_cairo_argb32` converts 4 pixels at a time via SSE2/SSSE3 intrinsics (`_mm_shuffle_epi8` / mask-shift) whenever all four pixels are opaque (`alpha == 255`), falling back per-pixel to premultiplied alpha only when transparent pixels occur. `poll_shm()` verifies `frame_index` with `memory_order_acquire` fences before and after conversion to guard against torn frames, and `on_canvas_draw` uses `CAIRO_FILTER_NEAREST` + `CAIRO_EXTEND_PAD` onto the 420×236 `GtkDrawingArea`.
3. **Reused Socket Transport & Progressive Startup Retry:** `IpcClient` reuses a persistent `GSocketClient`, `GSocketAddress`, and `GCancellable`, formats hot-path `btn` payloads without `nlohmann::json` DOM allocations, and reads newline-delimited JSON replies cleanly (`load` validation errors appear immediately). On cold daemon startup during cartridge selection, `TrayPlayView::schedule_load_retry()` polls socket readiness in short 40 ms increments (up to ~1 s) instead of a fixed blind sleep.

---

### 3.5. Keyboard Focus Isolation & Auto-Repeat Deduplication

#### Problem
- If GTK action buttons (`Load Game`, `Eject`, `Quit`) hold keyboard focus, pressing `Space` or `Enter` activates those buttons instead of sending `a` / `start` to the running cartridge, and `Arrow` keys move widget focus instead of moving the paddle.
- Holding movement keys (`W`/`S`/Arrows) on X11 generates repeated `key-press-event`s that would spam `/tmp/trayplay.sock`.

#### Solution
- Control buttons in the game view set `can_focus = FALSE`, and `m_canvas` (`GtkDrawingArea`) grabs focus whenever the game screen is shown.
- `TrayPlayView` tracks `m_pressed_buttons` (`std::unordered_set<std::string>`), suppressing redundant `key-press` repeats and automatically sending `pressed: false` for all held keys when the popup hides or enters the file browser.
- `map_key_to_button()` maps both Latin (`W`/`A`/`S`/`D`/`M`) and Cyrillic (`Ц`/`Ф`/`Ы`/`В`/`Ь`) keyvals so in-game controls work identically regardless of active keyboard layout.

---

### 3.6. Embedded Single-Source CSS & Hybrid Theme Styling

`frontends/xfce/style.css` is embedded directly into the binary at CMake configure time via `generated_style.h.in` (eliminating runtime path lookup failures whether launched from `./build/` or `/usr/bin/`). Outer chrome uses native GTK 3 theme variables (`@theme_bg_color`, `@theme_fg_color`, `@borders`), adapting seamlessly to both dark and light XFCE themes while keeping the embedded 16:9 CRT bezel and cartridge explorer dark and high-contrast.

---

### 3.7. Multithreaded Directory Scanning, UI State Caching & Safe Teardown

1. **Non-Blocking `CartridgeBrowser` (`GTask` Worker Pool):** Directory enumeration, per-subdirectory `game.json` probing, and `g_utf8_collate_key_for_filename()` sort-key precomputation run off the UI thread via `g_task_run_in_thread()` with a per-navigation `GCancellable` and generation counter, keeping the popover responsive on large directories or slow network mounts. `settings.json` writes are skipped unless `m_current_dir` actually changed.
2. **GTK 3 Widget/CSS Caching:** `TrayPlayView` caches title text, stack page name, placeholder strings/error state, rounded FPS integer, badge text, and active CSS class so the 60 FPS SHM polling loop never triggers redundant Pango layouts or CSS cascades when values remain unchanged.
3. **Signal-Safe Exit (`g_idle_add`):** `quit_application()` stops the daemon synchronously and defers destroying GTK widgets and `DbusmenuServer` to a GLib idle callback (`g_idle_add`) so objects are never torn down inside their own `clicked` / `item-activated` signal stack.

---

## 4. UI/UX Specifications

- **Screen Dimensions:** 420×236 px 16:9 widescreen viewport scaling the core's 320×180 RGBA32 framebuffer with nearest-neighbor filtering.
- **Status State Machine:**
  - **`● WAITING`** (Amber): Idle state, no cartridge loaded (`tray-console-daemon` uses 0% CPU).
  - **`● ACTIVE (<N> FPS)`** (Cyan): Cartridge active and streaming frames.
  - **`● PAUSED`** (Slate): Cartridge paused.
  - **`● ERROR`** (Red): Invalid cartridge, missing file, or Lua VM runtime exception.
  - **`● EXPLORER`** (Cyan): Embedded cartridge browser active.
- **Controls & Navigation:**
  - `Load Game (.gpak)`: Opens the embedded cartridge explorer (quick-jump shortcuts `Games`, `Home`, `Downloads`, unpacked `game.json` project detection, and native `GtkFileChooserNative` via `System...`).
  - `Eject`: Unloads the active cartridge and clears stale SHM if the daemon is offline.
  - **Keyboard:** `W`/`A`/`S`/`D` or Arrow keys (`up`/`left`/`down`/`right`), `Space` (`a`), `Enter` (`start`), `Esc` (`select` in game; closes browser in explorer; hides popup when idle), `M` (`m` mute toggle).
- **Tray Right-Click Menu (DBusMenu):**
  - `TrayPlay Microconsole` (header)
  - `Open Screen` / `Hide Screen`
  - `Eject Cartridge`
  - `Quit` (stops `tray-console-daemon` and exits `trayplay-xfce`)
