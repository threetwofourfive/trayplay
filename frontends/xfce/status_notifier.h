#pragma once

#include "ipc_client.h"

#include <gio/gio.h>
#include <gtk/gtk.h>

#ifdef HAVE_DBUSMENU
#include <libdbusmenu-glib/menuitem.h>
#include <libdbusmenu-glib/server.h>
#endif

#include <functional>
#include <memory>
#include <string>

namespace trayplay::xfce {

class StatusNotifier {
public:
    struct Callbacks {
        std::function<void(int anchor_x, int anchor_y)> on_activate;
        std::function<void()> on_open_screen;
        std::function<void()> on_eject;
        std::function<void()> on_quit;
    };

    StatusNotifier(GDBusConnection* connection, Callbacks callbacks);
    ~StatusNotifier();

    StatusNotifier(const StatusNotifier&) = delete;
    StatusNotifier& operator=(const StatusNotifier&) = delete;

    void set_status_info(DaemonState state, const std::string& title);
    void set_popup_visible(bool visible);

    static std::string resolve_tray_icon_name();

private:
    void register_sni_object();
    void register_dbusmenu();
    void call_register_with_watcher();
    void enable_fallback_status_icon();
    void disable_fallback_status_icon();
    void emit_sni_signal(const char* signal_name);
    void show_fallback_popup_menu(guint button, guint32 activate_time);

    static GVariant* on_get_property(GDBusConnection* conn,
                                     const gchar* sender,
                                     const gchar* object_path,
                                     const gchar* interface_name,
                                     const gchar* property_name,
                                     GError** error,
                                     gpointer user_data);

    static void on_method_call(GDBusConnection* conn,
                               const gchar* sender,
                               const gchar* object_path,
                               const gchar* interface_name,
                               const gchar* method_name,
                               GVariant* parameters,
                               GDBusMethodInvocation* invocation,
                               gpointer user_data);

    GDBusConnection* m_conn = nullptr;
    Callbacks m_callbacks;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
    GCancellable* m_cancellable = nullptr;

    guint m_sni_reg_id = 0;
    guint m_watcher_id = 0;
    guint m_fallback_timer_id = 0;
    bool m_registered_with_watcher = false;

    std::string m_icon_name;
    std::string m_tooltip_title = "TrayPlay";
    std::string m_tooltip_body = "Waiting for cartridge";

#ifdef HAVE_DBUSMENU
    DbusmenuServer* m_menu_server = nullptr;
    DbusmenuMenuitem* m_toggle_menu_item = nullptr;
#endif

    GtkStatusIcon* m_fallback_icon = nullptr;
    bool m_popup_visible = false;
};

} // namespace trayplay::xfce
