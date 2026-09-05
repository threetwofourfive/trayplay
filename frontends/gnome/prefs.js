import { ExtensionPreferences } from 'resource:///org/gnome/Shell/Extensions/js/extensions/prefs.js';
import Adw from 'gi://Adw';

export default class TrayPlayPreferences extends ExtensionPreferences {
    fillPreferencesWindow(window) {
        const page = new Adw.PreferencesPage();
        const group = new Adw.PreferencesGroup({
            title: 'TrayPlay Configuration',
            description: 'Settings for the virtual microconsole tray frontend',
        });
        page.add(group);
        window.add(page);
    }
}
