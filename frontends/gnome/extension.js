import { Extension } from 'resource:///org/gnome/shell/extensions/extension.js';

export default class TrayPlayExtension extends Extension {
    async enable() {
        try {
            const file = this.dir.get_child('app.js');
            const uri = `${file.get_uri()}?v=${Date.now()}`;
            const module = await import(uri);
            this._app = new module.TrayPlayApp(this);
            this._app.enable();
        } catch (e) {
            console.error(`[TrayPlay] Failed to load app.js: ${e}`);
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
