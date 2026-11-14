#include "popup_window.h"
#include "status_notifier.h"
#include "trayplay_view.h"

#include <libxfce4panel/libxfce4panel.h>

#ifdef GDK_WINDOWING_WAYLAND
#include <gdk/gdkwayland.h>
#endif

#include <memory>

using namespace trayplay::xfce;

namespace {

struct PanelPluginContext {
    XfcePanelPlugin* plugin = nullptr;
    GtkWidget* button = nullptr;
    GtkWidget* icon = nullptr;
    std::unique_ptr<TrayPlayView> view;
    std::unique_ptr<PopupWindow> popup;
    bool syncing_toggle = false;
};

void update_plugin_size(PanelPluginContext* ctx, gint panel_size) {
    if (!ctx || !ctx->plugin || !ctx->button || !ctx->icon) return;

    const gint icon_size = xfce_panel_plugin_get_icon_size(ctx->plugin);
    gtk_image_set_pixel_size(GTK_IMAGE(ctx->icon), icon_size > 0 ? icon_size : 16);

    const XfcePanelPluginMode mode = xfce_panel_plugin_get_mode(ctx->plugin);
    if (mode == XFCE_PANEL_PLUGIN_MODE_DESKBAR) {
        gtk_widget_set_size_request(ctx->button, -1, panel_size);
    } else {
        gtk_widget_set_size_request(ctx->button, panel_size, panel_size);
    }
}

void open_panel_popup(PanelPluginContext* ctx) {
    if (!ctx || !ctx->popup || !ctx->plugin) return;

    xfce_panel_plugin_block_autohide(ctx->plugin, TRUE);

    GtkWindow* win = ctx->popup->gtk_window();
#if LIBXFCE4PANEL_CHECK_VERSION(4, 19, 0)
#ifdef GDK_WINDOWING_WAYLAND
    GdkDisplay* display = gdk_display_get_default();
    if (display && GDK_IS_WAYLAND_DISPLAY(display)) {
        gtk_widget_show_all(GTK_WIDGET(win));
        xfce_panel_plugin_popup_window(ctx->plugin, win, ctx->button);
        ctx->popup->notify_shown_externally();
        return;
    }
#endif
#endif

    if (!gtk_widget_get_realized(GTK_WIDGET(win))) {
        gtk_widget_realize(GTK_WIDGET(win));
    }

    gint x = 0;
    gint y = 0;
    xfce_panel_plugin_position_widget(ctx->plugin, GTK_WIDGET(win), ctx->button, &x, &y);
    ctx->popup->show_at_exact(x, y);
}

void close_panel_popup(PanelPluginContext* ctx) {
    if (!ctx || !ctx->popup) return;
    ctx->popup->hide();
    if (ctx->plugin) {
        xfce_panel_plugin_block_autohide(ctx->plugin, FALSE);
    }
}

void on_button_toggled(GtkToggleButton* btn, gpointer data) {
    auto* ctx = static_cast<PanelPluginContext*>(data);
    if (ctx->syncing_toggle) return;

    if (gtk_toggle_button_get_active(btn)) {
        if (ctx->popup && ctx->popup->was_recently_hidden_by_focus_out()) {
            ctx->syncing_toggle = true;
            gtk_toggle_button_set_active(btn, FALSE);
            ctx->syncing_toggle = false;
            return;
        }
        open_panel_popup(ctx);
    } else {
        close_panel_popup(ctx);
    }
}

gboolean on_remote_event(XfcePanelPlugin*, const gchar* name, const GValue*, gpointer data) {
    auto* ctx = static_cast<PanelPluginContext*>(data);
    if (g_strcmp0(name, "popup") == 0) {
        if (ctx->popup && ctx->popup->is_visible()) {
            close_panel_popup(ctx);
        } else {
            open_panel_popup(ctx);
        }
        return TRUE;
    }
    return FALSE;
}

void on_free_data(XfcePanelPlugin*, gpointer data) {
    auto* ctx = static_cast<PanelPluginContext*>(data);
    delete ctx;
}

void trayplay_panel_construct_impl(XfcePanelPlugin* plugin) {
    auto* ctx = new PanelPluginContext();
    ctx->plugin = plugin;

    ctx->button = xfce_panel_create_toggle_button();
    gtk_widget_set_name(ctx->button, "trayplay-panel-button");
    gtk_widget_set_tooltip_text(ctx->button, "TrayPlay Microconsole");

    const std::string icon_name = StatusNotifier::resolve_tray_icon_name();
    ctx->icon = gtk_image_new_from_icon_name(icon_name.c_str(), GTK_ICON_SIZE_MENU);
    gtk_container_add(GTK_CONTAINER(ctx->button), ctx->icon);

    gtk_container_add(GTK_CONTAINER(plugin), ctx->button);
    xfce_panel_plugin_add_action_widget(plugin, ctx->button);
    xfce_panel_plugin_set_small(plugin, TRUE);

    TrayPlayView::Callbacks view_cbs;
    view_cbs.on_quit_requested = [ctx]() {
        close_panel_popup(ctx);
        if (ctx->view) {
            ctx->view->shutdown_daemon();
        }
    };
    view_cbs.on_hide_requested = [ctx]() {
        close_panel_popup(ctx);
    };
    view_cbs.on_modal_dialog_state = [ctx](bool active) {
        if (ctx->popup) {
            ctx->popup->set_modal_dialog_active(active);
        }
    };
    view_cbs.on_status_changed = [ctx](DaemonState state, const std::string& title) {
        if (!ctx->button) return;
        std::string tip = "TrayPlay Microconsole";
        if (state == DaemonState::RUNNING && !title.empty()) {
            tip += " — Playing: " + title;
        } else if (state == DaemonState::PAUSED && !title.empty()) {
            tip += " — Paused: " + title;
        } else if (state == DaemonState::CRASHED) {
            tip += " — Cartridge Error";
        }
        gtk_widget_set_tooltip_text(ctx->button, tip.c_str());
    };

    ctx->view = std::make_unique<TrayPlayView>(std::move(view_cbs));
    ctx->popup = std::make_unique<PopupWindow>(*ctx->view, false);
    ctx->popup->set_on_visibility_changed([ctx](bool visible) {
        if (!visible && ctx->plugin) {
            xfce_panel_plugin_block_autohide(ctx->plugin, FALSE);
        }
        if (ctx->button) {
            ctx->syncing_toggle = true;
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ctx->button), visible ? TRUE : FALSE);
            ctx->syncing_toggle = false;
        }
    });

    // Add panel right-click context actions
    GtkWidget* eject_item = gtk_menu_item_new_with_label("Eject Cartridge");
    g_signal_connect(eject_item, "activate", G_CALLBACK(+[](GtkMenuItem*, gpointer d) {
        auto* c = static_cast<PanelPluginContext*>(d);
        if (c->view) c->view->eject_cartridge();
    }), ctx);
    gtk_widget_show(eject_item);
    xfce_panel_plugin_menu_insert_item(plugin, GTK_MENU_ITEM(eject_item));

    GtkWidget* stop_item = gtk_menu_item_new_with_label("Stop Daemon");
    g_signal_connect(stop_item, "activate", G_CALLBACK(+[](GtkMenuItem*, gpointer d) {
        auto* c = static_cast<PanelPluginContext*>(d);
        close_panel_popup(c);
        if (c->view) c->view->shutdown_daemon();
    }), ctx);
    gtk_widget_show(stop_item);
    xfce_panel_plugin_menu_insert_item(plugin, GTK_MENU_ITEM(stop_item));

    update_plugin_size(ctx, xfce_panel_plugin_get_size(plugin));

    g_signal_connect(ctx->button, "toggled", G_CALLBACK(on_button_toggled), ctx);
    g_signal_connect(plugin, "size-changed", G_CALLBACK(+[](XfcePanelPlugin*, gint size, gpointer d) -> gboolean {
        update_plugin_size(static_cast<PanelPluginContext*>(d), size);
        return TRUE;
    }), ctx);
    g_signal_connect(plugin, "mode-changed", G_CALLBACK(+[](XfcePanelPlugin* p, XfcePanelPluginMode, gpointer d) {
        update_plugin_size(static_cast<PanelPluginContext*>(d), xfce_panel_plugin_get_size(p));
    }), ctx);
    g_signal_connect(plugin, "remote-event", G_CALLBACK(on_remote_event), ctx);
    g_signal_connect(plugin, "free-data", G_CALLBACK(on_free_data), ctx);

    gtk_widget_show_all(ctx->button);
}

void trayplay_panel_realize(XfcePanelPlugin *xpp) {
    g_return_if_fail(XFCE_IS_PANEL_PLUGIN(xpp));
    g_signal_handlers_disconnect_by_func(
        G_OBJECT(xpp),
        reinterpret_cast<gpointer>(trayplay_panel_realize),
        nullptr);
    trayplay_panel_construct_impl(xpp);
}

} // namespace

// Expanded explicitly instead of XFCE_PANEL_PLUGIN_REGISTER() because the C macro
// relies on implicit void* -> GCallback/XfcePanelPlugin* conversions invalid in C++.
extern "C" G_MODULE_EXPORT XfcePanelPlugin *
xfce_panel_module_construct(const gchar *xpp_name,
                            gint xpp_unique_id,
                            const gchar *xpp_display_name,
                            const gchar *xpp_comment,
                            gchar **xpp_arguments,
                            GdkScreen *xpp_screen) {
    g_return_val_if_fail(GDK_IS_SCREEN(xpp_screen), nullptr);
    g_return_val_if_fail(xpp_name != nullptr && xpp_unique_id != -1, nullptr);

    XfcePanelPlugin *xpp = XFCE_PANEL_PLUGIN(g_object_new(
        XFCE_TYPE_PANEL_PLUGIN,
        "name", xpp_name,
        "unique-id", xpp_unique_id,
        "display-name", xpp_display_name,
        "comment", xpp_comment,
        "arguments", xpp_arguments,
        nullptr));
    g_signal_connect_after(
        G_OBJECT(xpp),
        "realize",
        G_CALLBACK(trayplay_panel_realize),
        nullptr);
    return xpp;
}
