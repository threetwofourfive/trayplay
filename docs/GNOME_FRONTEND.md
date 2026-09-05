# GNOME Shell Frontend Architecture & Implementation

This document provides a comprehensive technical breakdown of the native GNOME Shell frontend for **TrayPlay**, detailing its architectural design, technical hurdles encountered in GNOME 45–50 on Wayland, and the engineering solutions implemented.

---

## 1. Architectural Motivation

On modern Linux systems running the GNOME Desktop Environment with the Wayland display protocol, top-panel integration differs fundamentally from traditional X11 environments:
- In Wayland, the GNOME Shell top panel (`Main.panel`) and its window management system are executed entirely within the compositor process (`gnome-shell`).
- External client applications cannot place arbitrary windows directly into the panel or draw native Clutter actor popovers using XEmbed or simple StatusNotifierItem (AppIndicator) protocols without visual discrepancies or compositor restrictions.
- To achieve a **100% native look and feel**, TrayPlay implements a first-class GNOME Shell Extension utilizing **GJS (GNOME JavaScript)**, **Clutter**, and **St (Shell Toolkit)**.

---

## 2. Directory Structure & Responsibilities

The GNOME frontend resides in `frontends/gnome/`:

```
frontends/gnome/
├── metadata.json       # Extension metadata, UUID, and supported Shell versions (45-50)
├── extension.js        # Lightweight dynamic loader (enables instant hot-reloading)
├── app.js              # Full frontend implementation (UI, popovers, gestures, state)
├── stylesheet.css      # Custom styling for the 16:9 screen, badges, and controls
└── prefs.js            # Libadwaita preferences dialog entry point
```

---

## 3. Key Technical Challenges & Engineering Solutions

During the development of this native frontend on GNOME 45–50, several low-level GJS and Mutter/Clutter challenges were identified and solved.

### 3.1. ES Module Caching in Wayland (The Hot-Reloading Problem)

#### Problem
In GNOME 45+, GNOME Shell transitioned completely to ECMAScript Modules (ESM). GNOME Shell's internal extension manager (`extensionSystem.js`) imports the root module once:
```javascript
extensionModule = await import(extensionJs.get_uri());
extension.isImported = true;
```
Because the SpiderMonkey JavaScript engine caches module URLs for the entire lifetime of the `gnome-shell` process, toggling the extension in the GNOME Extensions app (`disableExtension` / `enableExtension`) does **not** re-read `extension.js` from disk. Under Wayland (where restarting GNOME Shell via `Alt+F2` -> `r` is not possible), developers traditionally had to log out and log in for every single JavaScript modification.

#### Solution: Dynamic Cache-Busting Loader
We decoupled the extension into two layers:
1. `extension.js` serves as a permanent, immutable loader.
2. The core logic is placed in `app.js`.
3. In `extension.js`, `app.js` is imported dynamically with a unique timestamp query parameter:

```javascript
// extension.js
export default class TrayPlayExtension extends Extension {
    async enable() {
        const file = this.dir.get_child('app.js');
        const uri = `${file.get_uri()}?v=${Date.now()}`;
        const module = await import(uri);
        this._app = new module.TrayPlayApp(this);
        this._app.enable();
    }
    // ...
}
```
SpiderMonkey treats each unique URL as an entirely new module, guaranteeing that **all code changes in `app.js` and `stylesheet.css` take effect immediately upon toggling the extension** without requiring a session restart.

---

### 3.2. GLib GType Collision During Reloads

#### Problem
When `app.js` is re-imported dynamically, GJS attempts to re-register the custom GObject class (`TrayPlayIndicator`) with the GLib type system:
```javascript
export const TrayPlayIndicator = GObject.registerClass(
class TrayPlayIndicator extends PanelMenu.Button { ... });
```
Because GLib's static type registry (`g_type_register_static`) does not permit re-registering an existing type name, subsequent reloads threw a fatal error:
```
Error: Type name Gjs_trayplay_threetwofourfive_github_io_app_TrayPlayIndicator is already registered
```

#### Solution: Dynamic GTypeName Generation
We supply an explicit, uniquely versioned `GTypeName` upon each dynamic import:
```javascript
export const TrayPlayIndicator = GObject.registerClass({
    GTypeName: `TrayPlayIndicator_${Date.now()}`,
}, class TrayPlayIndicator extends PanelMenu.Button {
    // ...
});
```
This enables seamless, indefinite reloads in development without type collisions.

---

### 3.3. Multi-Button Gesture Handling in Clutter

#### Problem
`PanelMenu.Button` creates an internal `Clutter.ClickGesture` by default. In GJS / Mutter:
1. `Clutter.ClickGesture` defaults to `required_button = 0` (which matches **any** mouse button, consuming both LMB and RMB presses).
2. Attempting to restrict it via property assignment (`gesture.required_button = Clutter.BUTTON_PRIMARY`) silently fails because `required_button` is an internal C property without a writable JS setter property.
3. As a result, right-clicking on the panel icon triggered the primary left-click popover instead of the context menu.

#### Solution: Native C-Binding Setters & Event Phase Capture
1. We invoke the explicit C-method binding `set_required_button(Clutter.BUTTON_PRIMARY)` on the primary click gesture.
2. We instantiate a dedicated secondary gesture for right-click (`BUTTON_SECONDARY`) and attach it to the `Clutter.EventPhase.CAPTURE` phase:

```javascript
_setupClickHandling() {
    if (this._clickGesture) {
        this._clickGesture.set_required_button(Clutter.BUTTON_PRIMARY);
    }

    this._rightClickGesture = new Clutter.ClickGesture();
    this._rightClickGesture.set_recognize_on_press(true);
    this._rightClickGesture.set_required_button(Clutter.BUTTON_SECONDARY);
    this._rightClickGesture.connect('recognize', () => {
        if (this.menu?.isOpen)
            this.menu.close();
        this._contextMenu?.toggle();
    });
    this.add_action_full('trayplay-right-click', Clutter.EventPhase.CAPTURE, this._rightClickGesture);
}
```

---

### 3.4. Panel Menu Hover Cycling Isolation

#### Problem
In GNOME Shell, when any top-bar panel menu is open, moving the cursor across neighboring panel indicators automatically opens their menu without clicking (hover cycling).
- GNOME's global `Main.panel.menuManager` locates the menu associated with the hovered actor via `this._menus.find(m => m.sourceActor === actor)`.
- When the right-click context menu was registered in `Main.panel.menuManager`, it shadowed the primary screen popover, causing the context menu to unexpectedly pop open whenever the mouse hovered over TrayPlay from another extension.

#### Solution: Dedicated `PopupMenuManager`
We isolated the context menu from `Main.panel.menuManager`:
- The primary game popover (`this.menu`) remains the **only** menu registered with `Main.panel.menuManager`.
- The right-click context menu uses its own isolated `PopupMenuManager`:

```javascript
this._contextMenuManager = new PopupMenu.PopupMenuManager(this);
this._contextMenuManager.addMenu(this._contextMenu);
```
Result: Hover cycling across the top bar opens strictly the main screen popover. The context menu opens **only** when explicitly requested via RMB.

---

### 3.5. Pango Text Metric Ellipsization (`...`)

#### Problem
Certain labels (`TRAYPLAY` in the header and `▶ TRAYPLAY ◀` on the canvas) intermittently displayed as `traypl...` and `▶ Trayplay...`.
- In GNOME Shell, `St.Label` defaults to `ellipsize = Pango.EllipsizeMode.END`.
- When CSS specifies properties like `letter-spacing` or heavy font weights (`font-weight: 900`), Clutter allocates width based on standard glyph advance, but Pango renders with extra spacing. When the rendered output overflows the allocated box by even 1 pixel, Pango aggressively truncates the string with an ellipsis.

#### Solution
1. Explicitly set `clutter_text.ellipsize = Pango.EllipsizeMode.NONE` on critical labels.
2. Removed CSS `letter-spacing` rules in favor of clean font rendering:
```javascript
titleLabel.clutter_text.ellipsize = Pango.EllipsizeMode.NONE;
this._screenTitle.clutter_text.ellipsize = Pango.EllipsizeMode.NONE;
```

---

### 3.6. Subprocess Destruction & Memory Safety

#### Problem
Launching external asynchronous processes (such as the `zenity` native file selection dialog) creates a potential memory leak or null pointer exception if the user disables the extension while the file chooser dialog is still open.

#### Solution
We implemented a lifecycle destruction guard (`this._isDestroyed`):
```javascript
proc.communicate_utf8_async(null, null, (proc, res) => {
    if (this._isDestroyed)
        return;
    // Safely update UI actors...
});
```
On `destroy()`, `this._isDestroyed` is set to `true`, and all actions and actors are cleanly unparented and destroyed.

---

## 4. UI/UX Specifications

- **Screen Dimensions:** Fixed 420x236 px frame (native 16:9 widescreen ratio, targeting internal 320x180 canvas).
- **Status State Machine:**
  - **`● WAITING`** (Yellow, `#f1c40f`): Idle state, waiting for a `.gpak` cartridge.
  - **`● ACTIVE`** (Cyan, `#00ffcc`): Cartridge loaded and running.
  - **`● ERROR`** (Red, `#ff4757`): Invalid file format or read error.
- **Controls:**
  - `Load Game (.gpak)`: Invokes async native file chooser filtered by `.gpak`.
  - `Eject`: Unloads the active cartridge and resets the state machine to `WAITING`.
- **Context Menu (RMB):**
  - `Open Screen`: Opens primary game popover.
  - `Quit`: Terminates the background C++ daemon (`pkill tray-console-daemon`) and formally disables the extension via `Main.extensionManager.disableExtension()`.
