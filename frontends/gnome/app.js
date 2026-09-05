import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import * as PanelMenu from 'resource:///org/gnome/shell/ui/panelMenu.js';
import * as PopupMenu from 'resource:///org/gnome/shell/ui/popupMenu.js';
import St from 'gi://St';
import Clutter from 'gi://Clutter';
import GObject from 'gi://GObject';
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import Pango from 'gi://Pango';

export const TrayPlayIndicator = GObject.registerClass({
    GTypeName: `TrayPlayIndicator_${Date.now()}`,
}, class TrayPlayIndicator extends PanelMenu.Button {
    _init(extension) {
        super._init(0.5, 'TrayPlay', false);
        this._extension = extension;
        this._currentCartridge = null;

        // Tray Icon: Native system gamepad / joystick icon
        this._icon = new St.Icon({
            icon_name: 'input-gaming-symbolic',
            style_class: 'system-status-icon',
        });
        this.add_child(this._icon);

        // Build Left-Click Popover Menu (Main UI)
        this._buildMainPopover();

        // Build Right-Click Context Menu
        this._buildContextMenu();

        // Setup click dispatching: LMB -> Game Popover, RMB -> Context Menu
        this._setupClickHandling();
    }

    _buildMainPopover() {
        this.menu.box.add_style_class_name('trayplay-popup-box');
        this.menu.connect('open-state-changed', (menu, open) => {
            if (open && this._contextMenu?.isOpen) {
                this._contextMenu.close();
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

        // 1. Header (Title & Status LED)
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
        const titleLabel = new St.Label({
            text: 'TRAYPLAY',
            style_class: 'trayplay-title',
            y_align: Clutter.ActorAlign.CENTER,
        });
        titleLabel.clutter_text.ellipsize = Pango.EllipsizeMode.NONE;
        headerBox.add_child(titleIcon);
        headerBox.add_child(titleLabel);

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

        // 2. Wide Screen / Canvas Field
        const screenFrame = new St.BoxLayout({
            style_class: 'trayplay-screen-frame',
            x_expand: true,
        });

        this._screen = new St.BoxLayout({
            vertical: true,
            style_class: 'trayplay-screen',
            x_expand: true,
            y_expand: true,
        });

        // Screen Content (Centered Display)
        const centerBox = new St.BoxLayout({
            vertical: true,
            x_align: Clutter.ActorAlign.CENTER,
            y_align: Clutter.ActorAlign.CENTER,
            x_expand: true,
            y_expand: true,
        });

        this._screenTitle = new St.Label({
            text: '▶ TRAYPLAY ◀',
            style_class: 'trayplay-screen-title',
            x_align: Clutter.ActorAlign.CENTER,
        });
        this._screenTitle.clutter_text.ellipsize = Pango.EllipsizeMode.NONE;

        this._screenStatus = new St.Label({
            text: '[ NO CARTRIDGE LOADED ]',
            style_class: 'trayplay-screen-status',
            x_align: Clutter.ActorAlign.CENTER,
        });
        this._screenStatus.clutter_text.ellipsize = Pango.EllipsizeMode.NONE;

        centerBox.add_child(this._screenTitle);
        centerBox.add_child(this._screenStatus);
        this._screen.add_child(centerBox);

        // Screen Info Bar (Bottom metadata)
        const infoBar = new St.BoxLayout({
            vertical: false,
            style_class: 'trayplay-screen-info-bar',
            x_expand: true,
        });
        const displayLabel = new St.Label({ text: 'DISPLAY: 320x180' });
        displayLabel.clutter_text.ellipsize = Pango.EllipsizeMode.NONE;
        infoBar.add_child(displayLabel);
        this._screen.add_child(infoBar);

        screenFrame.add_child(this._screen);
        rootBox.add_child(screenFrame);

        // 3. Controls (Below the screen)
        const controlsBox = new St.BoxLayout({
            vertical: false,
            style_class: 'trayplay-controls',
            x_expand: true,
        });

        // "Load Game" Button
        this._loadButton = new St.Button({
            label: 'Load Game (.gpak)',
            style_class: 'trayplay-load-btn button',
            can_focus: true,
            x_expand: true,
        });
        this._loadButton.connect('clicked', () => this._onLoadClicked());
        controlsBox.add_child(this._loadButton);

        // "Eject / Reset" Button
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

        // Use dedicated PopupMenuManager so Main.panel.menuManager only knows about this.menu
        this._contextMenuManager = new PopupMenu.PopupMenuManager(this);
        this._contextMenuManager.addMenu(this._contextMenu);

        // Header in context menu
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

            // 1. Terminate the C++ background daemon if running
            try {
                Gio.Subprocess.new(['pkill', '-f', 'tray-console-daemon'], Gio.SubprocessFlags.NONE);
            } catch (e) {
                // Ignore if process not running
            }

            // 2. Formally disable the extension in GNOME Shell (updates GNOME Extensions app toggle)
            if (Main.extensionManager && typeof Main.extensionManager.disableExtension === 'function') {
                Main.extensionManager.disableExtension(this._extension.uuid);
            } else {
                this._extension.disable();
            }
        });
        this._contextMenu.addMenuItem(exitItem);
    }

    _setupClickHandling() {
        // Constrain standard left-click gesture to Primary button (Left Click)
        if (this._clickGesture) {
            this._clickGesture.set_required_button(Clutter.BUTTON_PRIMARY);
        }

        // Add secondary gesture for Right Click (Secondary button)
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

    _onLoadClicked() {
        try {
            // Asynchronously launch native file chooser via zenity
            const proc = Gio.Subprocess.new(
                [
                    'zenity',
                    '--file-selection',
                    '--title=Select TrayPlay Cartridge (.gpak)',
                    '--file-filter=Game Cartridge (*.gpak) | *.gpak',
                    '--file-filter=All Files | *',
                ],
                Gio.SubprocessFlags.STDOUT_PIPE | Gio.SubprocessFlags.STDERR_PIPE
            );

            proc.communicate_utf8_async(null, null, (proc, res) => {
                if (this._isDestroyed) {
                    return;
                }
                try {
                    const [, stdout] = proc.communicate_utf8_finish(res);
                    if (stdout && stdout.trim().length > 0) {
                        const filePath = stdout.trim();
                        // Validate file extension
                        if (!filePath.toLowerCase().endsWith('.gpak')) {
                            this._setErrorState();
                            return;
                        }
                        const file = Gio.File.new_for_path(filePath);
                        if (!file.query_exists(null)) {
                            this._setErrorState();
                            return;
                        }
                        const fileName = GLib.path_get_basename(filePath);
                        this._setLoadedCartridge(fileName);
                    }
                } catch (e) {
                    this._setErrorState();
                }
            });
        } catch (e) {
            this._setErrorState();
        }
    }

    _setWaitingState() {
        this._currentCartridge = null;
        this._screenTitle.text = '▶ TRAYPLAY ◀';
        this._screenStatus.text = '[ NO CARTRIDGE LOADED ]';
        this._screenStatus.style = 'color: #f1c40f;';
        this._statusBadge.text = '● WAITING';
        this._statusBadge.style = 'color: #f1c40f; background-color: rgba(241, 196, 15, 0.15);';
    }

    _setLoadedCartridge(name) {
        this._currentCartridge = name;
        this._screenTitle.text = '▶ GAME RUNNING ◀';
        this._screenStatus.text = `[ CARTRIDGE: ${name.toUpperCase()} ]`;
        this._screenStatus.style = 'color: #2ecc71;';
        this._statusBadge.text = '● ACTIVE';
        this._statusBadge.style = 'color: #00ffcc; background-color: rgba(0, 255, 204, 0.15);';
    }

    _setErrorState() {
        this._currentCartridge = null;
        this._screenTitle.text = '▶ ERROR ◀';
        this._screenStatus.text = '[ CARTRIDGE ERROR ]';
        this._screenStatus.style = 'color: #ff4757;';
        this._statusBadge.text = '● ERROR';
        this._statusBadge.style = 'color: #ff4757; background-color: rgba(255, 71, 87, 0.18);';
    }

    _onEjectClicked() {
        this._setWaitingState();
    }

    destroy() {
        this._isDestroyed = true;
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
