import { Extension } from 'resource:///org/gnome/shell/extensions/extension.js';
import { TrayPlayApp } from './app.js';

export default class TrayPlayExtension extends Extension {
    enable() {
        try {
            this._app = new TrayPlayApp(this);
            this._app.enable();
        } catch (e) {
            console.error(`[TrayPlay] Failed to initialize extension: ${e}`);
        }
    }

    disable() {
        if (this._app) {
            try {
                this._app.disable();
            } catch (e) {
                console.error(`[TrayPlay] Error during disable: ${e}`);
            }
            this._app = null;
        }
    }
}
