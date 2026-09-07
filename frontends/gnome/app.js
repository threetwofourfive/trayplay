import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import * as PanelMenu from 'resource:///org/gnome/shell/ui/panelMenu.js';
import * as PopupMenu from 'resource:///org/gnome/shell/ui/popupMenu.js';
import St from 'gi://St';
import Clutter from 'gi://Clutter';
import GObject from 'gi://GObject';
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import Pango from 'gi://Pango';
import GdkPixbuf from 'gi://GdkPixbuf';
import Gdk from 'gi://Gdk';
import Cairo from 'gi://cairo';

const SHM_PATH = '/dev/shm/trayplay_framebuffer';
const SOCKET_PATH = '/tmp/trayplay.sock';
const HEADER_SIZE = 100;
const VIRTUAL_WIDTH = 320;
const VIRTUAL_HEIGHT = 180;
const BUFFER_SIZE = VIRTUAL_WIDTH * VIRTUAL_HEIGHT * 4;

export const TrayPlayIndicator = GObject.registerClass({
    GTypeName: `TrayPlayIndicator_${Date.now()}`,
}, class TrayPlayIndicator extends PanelMenu.Button {
    _init(extension) {
        super._init(0.5, 'TrayPlay', false);
        this._extension = extension;
        this._isDestroyed = false;

        this._lastFrameIndex = -1n;
        this._pollTimerId = 0;
        this._currentPixbuf = null;
        this._currentTitle = 'TRAYPLAY';

        // Tray Icon: System gaming icon
        this._icon = new St.Icon({
            icon_name: 'input-gaming-symbolic',
            style_class: 'system-status-icon',
        });
        this.add_child(this._icon);

        // Build Left-Click Popover Menu (Main UI)
        this._buildMainPopover();

        // Build Right-Click Context Menu
        this._buildContextMenu();

        // Setup mouse gestures & keyboard controls
        this._setupClickHandling();
        this._setupKeyHandling();
    }

    _buildMainPopover() {
        this.menu.box.add_style_class_name('trayplay-popup-box');
        this.menu.actor.reactive = true;
        this.menu.actor.can_focus = true;

        this.menu.connect('open-state-changed', (menu, open) => {
            if (open) {
                if (this._contextMenu?.isOpen) {
                    this._contextMenu.close();
                }
                this._ensureDaemon();
                this._startPolling();
                this.menu.actor.grab_key_focus();
            } else {
                this._stopPolling();
            }
        });

        const mainItem = new PopupMenu.PopupBaseMenuItem({
            reactive: false,
            can_focus: false,
        });

        const rootBox = new St.BoxLayout({
            vertical: true,
            x_expand: true,
            y_expand: true,
            style_class: 'trayplay-root-box',
        });

        // 1. Header (Title & Status Badge)
        const headerBox = new St.BoxLayout({
            vertical: false,
            x_expand: true,
            style_class: 'trayplay-header',
        });

        const titleIcon = new St.Icon({
            icon_name: 'input-gaming-symbolic',
            icon_size: 16,
            style: 'margin-right: 6px;',
        });
        this._titleLabel = new St.Label({
            text: 'TRAYPLAY',
            style_class: 'trayplay-title',
            y_align: Clutter.ActorAlign.CENTER,
        });
        this._titleLabel.clutter_text.ellipsize = Pango.EllipsizeMode.NONE;
        headerBox.add_child(titleIcon);
        headerBox.add_child(this._titleLabel);

        const spacer = new St.Widget({ x_expand: true });
        headerBox.add_child(spacer);

        this._statusBadge = new St.Label({
            text: '● WAITING',
            style_class: 'trayplay-status-badge',
            y_align: Clutter.ActorAlign.CENTER,
        });
        this._statusBadge.clutter_text.ellipsize = Pango.EllipsizeMode.NONE;
        headerBox.add_child(this._statusBadge);
        rootBox.add_child(headerBox);

        // 2. Wide Screen / Canvas Frame (420x236 16:9)
        const screenFrame = new St.BoxLayout({
            style_class: 'trayplay-screen-frame',
            x_expand: true,
        });

        this._screenContainer = new St.Widget({
            style_class: 'trayplay-screen',
            layout_manager: new Clutter.BinLayout(),
            x_expand: true,
            y_expand: true,
        });

        // Live Drawing Area (Canvas)
        this._canvas = new St.DrawingArea({
            width: 420,
            height: 236,
            x_expand: true,
            y_expand: true,
            reactive: true,
        });
        this._canvas.connect('repaint', (area) => this._onCanvasRepaint(area));
        this._screenContainer.add_child(this._canvas);

        // Placeholder Box (Shown when waiting for cartridge)
        this._placeholderBox = new St.BoxLayout({
            vertical: true,
            x_align: Clutter.ActorAlign.CENTER,
            y_align: Clutter.ActorAlign.CENTER,
            x_expand: true,
            y_expand: true,
        });

        this._placeholderTitle = new St.Label({
            text: '▶ TRAYPLAY ◀',
            style_class: 'trayplay-screen-title',
            x_align: Clutter.ActorAlign.CENTER,
        });
        this._placeholderTitle.clutter_text.ellipsize = Pango.EllipsizeMode.NONE;

        this._placeholderStatus = new St.Label({
            text: '[ NO CARTRIDGE LOADED ]',
            style_class: 'trayplay-screen-status',
            x_align: Clutter.ActorAlign.CENTER,
        });
        this._placeholderStatus.clutter_text.ellipsize = Pango.EllipsizeMode.NONE;

        this._placeholderBox.add_child(this._placeholderTitle);
        this._placeholderBox.add_child(this._placeholderStatus);
        this._screenContainer.add_child(this._placeholderBox);

        screenFrame.add_child(this._screenContainer);
        rootBox.add_child(screenFrame);

        // 3. Controls
        const controlsBox = new St.BoxLayout({
            vertical: false,
            style_class: 'trayplay-controls',
            x_expand: true,
        });

        this._loadButton = new St.Button({
            label: 'Load Game (.gpak)',
            style_class: 'trayplay-load-btn button',
            can_focus: true,
            x_expand: true,
        });
        this._loadButton.connect('clicked', () => this._onLoadClicked());
        controlsBox.add_child(this._loadButton);

        this._resetButton = new St.Button({
            label: 'Eject',
            style_class: 'trayplay-action-btn button',
            can_focus: true,
        });
        this._resetButton.connect('clicked', () => this._onEjectClicked());
        controlsBox.add_child(this._resetButton);

        rootBox.add_child(controlsBox);
        mainItem.add_child(rootBox);
        this.menu.addMenuItem(mainItem);
    }

    _buildContextMenu() {
        this._contextMenu = new PopupMenu.PopupMenu(this, 0.5, St.Side.TOP);
        this._contextMenu.actor.add_style_class_name('panel-menu');
        this._contextMenu.connect('open-state-changed', (menu, open) => {
            if (open) {
                this.add_style_pseudo_class('active');
                if (this.menu?.isOpen) {
                    this.menu.close();
                }
            } else {
                if (!this.menu?.isOpen) {
                    this.remove_style_pseudo_class('active');
                }
            }
        });
        Main.uiGroup.add_child(this._contextMenu.actor);
        this._contextMenu.actor.hide();

        this._contextMenuManager = new PopupMenu.PopupMenuManager(this);
        this._contextMenuManager.addMenu(this._contextMenu);

        const titleItem = new PopupMenu.PopupMenuItem('🎮 TrayPlay Microconsole', { reactive: false });
        titleItem.label.clutter_text.set_markup('<b>🎮 TrayPlay Microconsole</b>');
        this._contextMenu.addMenuItem(titleItem);

        const openItem = new PopupMenu.PopupMenuItem('Open Screen');
        openItem.connect('activate', () => {
            this._contextMenu.close();
            this.menu.open();
        });
        this._contextMenu.addMenuItem(openItem);

        const sep = new PopupMenu.PopupSeparatorMenuItem();
        this._contextMenu.addMenuItem(sep);

        const exitItem = new PopupMenu.PopupMenuItem('Quit');
        exitItem.connect('activate', () => {
            this._contextMenu.close();
            this._sendIpcCommand({ cmd: 'quit' });
            try {
                Gio.Subprocess.new(['pkill', '-f', 'tray-console-daemon'], Gio.SubprocessFlags.NONE);
            } catch (e) {
                // Ignore
            }

            if (Main.extensionManager && typeof Main.extensionManager.disableExtension === 'function') {
                Main.extensionManager.disableExtension(this._extension.uuid);
            } else {
                this._extension.disable();
            }
        });
        this._contextMenu.addMenuItem(exitItem);
    }

    _setupClickHandling() {
        if (this._clickGesture) {
            this._clickGesture.set_required_button(Clutter.BUTTON_PRIMARY);
        }

        this._rightClickGesture = new Clutter.ClickGesture();
        this._rightClickGesture.set_recognize_on_press(true);
        this._rightClickGesture.set_required_button(Clutter.BUTTON_SECONDARY);
        this._rightClickGesture.connect('recognize', () => {
            if (this.menu && this.menu.isOpen) {
                this.menu.close();
            }
            this._contextMenu?.toggle();
        });
        this.add_action_full('trayplay-right-click', Clutter.EventPhase.CAPTURE, this._rightClickGesture);
    }

    _setupKeyHandling() {
        this.menu.actor.connect('key-press-event', (actor, event) => {
            const btn = this._mapKeyCode(event.get_key_symbol());
            if (btn) {
                this._sendIpcCommand({ cmd: 'btn', name: btn, pressed: true });
                return Clutter.EVENT_STOP;
            }
            return Clutter.EVENT_PROPAGATE;
        });

        this.menu.actor.connect('key-release-event', (actor, event) => {
            const btn = this._mapKeyCode(event.get_key_symbol());
            if (btn) {
                this._sendIpcCommand({ cmd: 'btn', name: btn, pressed: false });
                return Clutter.EVENT_STOP;
            }
            return Clutter.EVENT_PROPAGATE;
        });
    }

    _mapKeyCode(sym) {
        switch (sym) {
            case Clutter.KEY_Up:
            case Clutter.KEY_w:
            case Clutter.KEY_W:
                return 'up';
            case Clutter.KEY_Down:
            case Clutter.KEY_s:
            case Clutter.KEY_S:
                return 'down';
            case Clutter.KEY_Left:
            case Clutter.KEY_a:
            case Clutter.KEY_A:
                return 'left';
            case Clutter.KEY_Right:
            case Clutter.KEY_d:
            case Clutter.KEY_D:
                return 'right';
            case Clutter.KEY_space:
                return 'a';
            case Clutter.KEY_Return:
                return 'start';
            case Clutter.KEY_Escape:
                return 'select';
            case Clutter.KEY_m:
            case Clutter.KEY_M:
                return 'm';
            default:
                return null;
        }
    }

    _onCanvasRepaint(area) {
        const cr = area.get_context();
        if (this._currentPixbuf) {
            // Pixel-perfect nearest neighbor upscale to fit 420x236 container
            cr.scale(420 / VIRTUAL_WIDTH, 236 / VIRTUAL_HEIGHT);
            Gdk.cairo_set_source_pixbuf(cr, this._currentPixbuf, 0, 0);
            cr.getSource().setFilter(Cairo.Filter.NEAREST);
            cr.paint();
        } else {
            // Dark CRT background
            cr.setSourceRGB(0.05, 0.06, 0.08);
            cr.paint();
        }
    }

    _startPolling() {
        if (this._pollTimerId !== 0) return;

        this._pollTimerId = GLib.timeout_add(GLib.PRIORITY_DEFAULT, 16, () => {
            if (this._isDestroyed || !this.menu?.isOpen) {
                this._pollTimerId = 0;
                return GLib.SOURCE_REMOVE;
            }
            this._pollFrame();
            return GLib.SOURCE_CONTINUE;
        });
    }

    _stopPolling() {
        if (this._pollTimerId !== 0) {
            GLib.source_remove(this._pollTimerId);
            this._pollTimerId = 0;
        }
    }

    _pollFrame() {
        if (!GLib.file_test(SHM_PATH, GLib.FileTest.EXISTS)) {
            this._setWaitingState();
            return;
        }

        try {
            const mappedFile = GLib.MappedFile.new(SHM_PATH, false);
            const bytes = mappedFile.get_bytes();
            const totalSize = bytes.get_size();

            if (totalSize < HEADER_SIZE + BUFFER_SIZE) {
                return;
            }

            const arr = bytes.toArray();
            const view = new DataView(arr.buffer, arr.byteOffset, arr.byteLength);

            const magic = view.getUint32(0, true);
            if (magic !== 0x54524159) {
                return; // Not our magic header
            }

            const frameIndex = view.getBigUint64(20, true);
            const state = view.getUint32(28, true);
            const fps = view.getFloat32(32, true);

            // Read title string from byte 36 to 99
            const titleRaw = new TextDecoder().decode(arr.subarray(36, 100));
            const title = titleRaw.replace(/\0.*$/, '').trim() || 'TRAYPLAY';

            if (state === 1) { // RUNNING
                if (frameIndex !== this._lastFrameIndex) {
                    this._lastFrameIndex = frameIndex;
                    this._staleFrameTicks = 0;

                    const pixelSlice = arr.subarray(HEADER_SIZE, HEADER_SIZE + BUFFER_SIZE);
                    const pixelBytes = new GLib.Bytes(pixelSlice);

                    this._currentPixbuf = GdkPixbuf.Pixbuf.new_from_bytes(
                        pixelBytes,
                        GdkPixbuf.Colorspace.RGB,
                        true,
                        8,
                        VIRTUAL_WIDTH,
                        VIRTUAL_HEIGHT,
                        VIRTUAL_WIDTH * 4
                    );

                    this._canvas.queue_repaint();
                } else {
                    this._staleFrameTicks = (this._staleFrameTicks || 0) + 1;
                    // If no new frames for ~1.5s, verify daemon responsiveness
                    if (this._staleFrameTicks > 90) {
                        this._setWaitingState();
                        return;
                    }
                }

                if (this._currentPixbuf) {
                    if (this._placeholderBox.visible) {
                        this._placeholderBox.hide();
                        this._canvas.show();
                    }

                    this._titleLabel.text = title.toUpperCase();
                    const fpsText = Math.round(fps);
                    this._statusBadge.text = `● ACTIVE (${fpsText} FPS)`;
                    this._statusBadge.style = 'color: #00ffcc; background-color: rgba(0, 255, 204, 0.15);';
                } else {
                    this._setWaitingState();
                }
            } else if (state === 3) { // CRASHED
                this._setErrorState();
            } else if (state === 2) { // PAUSED
                this._statusBadge.text = '● PAUSED';
                this._statusBadge.style = 'color: #a4b0be; background-color: rgba(164, 176, 190, 0.15);';
            } else { // STOPPED (0)
                this._setWaitingState();
            }
        } catch (e) {
            // Ignore temporary mapping read collisions
        }
    }

    _setWaitingState() {
        this._currentPixbuf = null;
        this._lastFrameIndex = -1n;
        this._staleFrameTicks = 0;
        this._titleLabel.text = 'TRAYPLAY';
        this._canvas.hide();
        this._placeholderBox.show();
        this._placeholderTitle.text = '▶ TRAYPLAY ◀';
        this._placeholderStatus.text = '[ NO CARTRIDGE LOADED ]';
        this._statusBadge.text = '● WAITING';
        this._statusBadge.style = 'color: #f1c40f; background-color: rgba(241, 196, 15, 0.15);';
    }

    _setErrorState(errorMsg) {
        this._currentPixbuf = null;
        this._lastFrameIndex = -1n;
        this._staleFrameTicks = 0;
        this._canvas.hide();
        this._placeholderBox.show();
        this._placeholderTitle.text = '▶ ERROR ◀';
        this._placeholderStatus.text = errorMsg ? `[ ${errorMsg.toUpperCase()} ]` : '[ CARTRIDGE CRASHED ]';
        this._statusBadge.text = '● ERROR';
        this._statusBadge.style = 'color: #ff4757; background-color: rgba(255, 71, 87, 0.18);';
    }

    _ensureDaemon() {
        if (!GLib.file_test(SOCKET_PATH, GLib.FileTest.EXISTS)) {
            this._spawnDaemon();
            return;
        }

        // Test if socket is genuinely responsive (not a stale socket from a dead process)
        try {
            const client = new Gio.SocketClient();
            const addr = Gio.UnixSocketAddress.new(SOCKET_PATH);
            client.connect_async(addr, null, (source, res) => {
                try {
                    const conn = client.connect_finish(res);
                    conn.close(null);
                } catch (e) {
                    // Stale dead socket: clean up and spawn fresh daemon
                    try {
                        GLib.unlink(SOCKET_PATH);
                    } catch (_) {}
                    this._spawnDaemon();
                }
            });
        } catch (e) {
            this._spawnDaemon();
        }
    }

    _spawnDaemon() {
        try {
            const possiblePaths = [
                GLib.build_filenamev([GLib.get_home_dir(), 'Рабочий_стол/trayplay/build/tray-console-daemon']),
                '/usr/bin/tray-console-daemon',
                '/usr/local/bin/tray-console-daemon'
            ];

            let daemonBin = null;
            for (const p of possiblePaths) {
                if (GLib.file_test(p, GLib.FileTest.IS_EXECUTABLE)) {
                    daemonBin = p;
                    break;
                }
            }

            if (daemonBin) {
                Gio.Subprocess.new([daemonBin], Gio.SubprocessFlags.NONE);
            }
        } catch (e) {
            // Daemon launch fallback
        }
    }

    _sendIpcCommand(commandObj, callback = null) {
        if (!GLib.file_test(SOCKET_PATH, GLib.FileTest.EXISTS)) {
            if (callback) callback(false, 'Socket not found');
            return;
        }

        try {
            const client = new Gio.SocketClient();
            const addr = Gio.UnixSocketAddress.new(SOCKET_PATH);
            client.connect_async(addr, null, (source, res) => {
                try {
                    const conn = client.connect_finish(res);
                    const outStream = conn.get_output_stream();
                    const payload = JSON.stringify(commandObj) + '\n';
                    outStream.write_all_async(
                        new TextEncoder().encode(payload),
                        GLib.PRIORITY_DEFAULT,
                        null,
                        () => {
                            conn.close(null);
                            if (callback) callback(true, 'OK');
                        }
                    );
                } catch (e) {
                    // Socket communication error or connection refused
                    try {
                        GLib.unlink(SOCKET_PATH);
                    } catch (_) {}
                    if (callback) callback(false, e.message);
                }
            });
        } catch (e) {
            if (callback) callback(false, e.message);
        }
    }

    _onLoadClicked() {
        try {
            const proc = Gio.Subprocess.new(
                [
                    'zenity',
                    '--file-selection',
                    '--title=Select TrayPlay Cartridge (.gpak, .lua)',
                    '--file-filter=Game Cartridge (*.gpak, *.lua) | *.gpak *.lua',
                    '--file-filter=All Files | *',
                ],
                Gio.SubprocessFlags.STDOUT_PIPE | Gio.SubprocessFlags.STDERR_PIPE
            );

            proc.communicate_utf8_async(null, null, (proc, res) => {
                if (this._isDestroyed) return;
                try {
                    const [, stdout] = proc.communicate_utf8_finish(res);
                    if (stdout && stdout.trim().length > 0) {
                        const filePath = stdout.trim();
                        this._sendIpcCommand({ cmd: 'load', path: filePath }, (success) => {
                            if (!success) {
                                // If sending failed, retry once after ensuring daemon
                                this._ensureDaemon();
                                GLib.timeout_add(GLib.PRIORITY_DEFAULT, 300, () => {
                                    this._sendIpcCommand({ cmd: 'load', path: filePath });
                                    return GLib.SOURCE_REMOVE;
                                });
                            }
                        });
                        GLib.idle_add(GLib.PRIORITY_DEFAULT, () => {
                            if (!this._isDestroyed && !this.menu.isOpen) {
                                this.menu.open();
                            }
                            return GLib.SOURCE_REMOVE;
                        });
                    }
                } catch (e) {
                    this._setErrorState();
                }
            });
        } catch (e) {
            this._setErrorState();
        }
    }

    _onEjectClicked() {
        this._setWaitingState();
        this._sendIpcCommand({ cmd: 'unload' }, (success) => {
            if (!success) {
                // If daemon is unreachable or dead, clear SHM so pollFrame never reads stale running state
                try {
                    GLib.unlink(SHM_PATH);
                } catch (_) {}
            }
        });
    }

    destroy() {
        this._isDestroyed = true;
        this._stopPolling();

        if (this._rightClickGesture) {
            this.remove_action(this._rightClickGesture);
            this._rightClickGesture = null;
        }
        if (this._contextMenu) {
            this._contextMenu.destroy();
            this._contextMenu = null;
        }
        this._contextMenuManager = null;
        super.destroy();
    }
});

export class TrayPlayApp {
    constructor(extension) {
        this._extension = extension;
        this._indicator = null;
    }

    enable() {
        this._indicator = new TrayPlayIndicator(this._extension);
        Main.panel.addToStatusArea(this._extension.uuid, this._indicator, 0, 'right');
    }

    disable() {
        if (this._indicator) {
            this._indicator.destroy();
            this._indicator = null;
        }
    }
}
