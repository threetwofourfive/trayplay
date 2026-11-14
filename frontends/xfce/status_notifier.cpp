#include "status_notifier.h"

namespace trayplay::xfce {

namespace {

constexpr const char* SNI_OBJECT_PATH = "/StatusNotifierItem";
constexpr const char* DBUSMENU_OBJECT_PATH = "/MenuBar";
constexpr const char* SNI_INTERFACE = "org.kde.StatusNotifierItem";
constexpr const char* WATCHER_BUS = "org.kde.StatusNotifierWatcher";
constexpr const char* WATCHER_PATH = "/StatusNotifierWatcher";
constexpr const char* WATCHER_IFACE = "org.kde.StatusNotifierWatcher";

constexpr const char* SNI_INTROSPECTION_XML = R"XML(
<node>
  <interface name="org.kde.StatusNotifierItem">
    <method name="Activate">
      <arg type="i" name="x" direction="in"/>
      <arg type="i" name="y" direction="in"/>
    </method>
    <method name="SecondaryActivate">
      <arg type="i" name="x" direction="in"/>
      <arg type="i" name="y" direction="in"/>
    </method>
    <method name="ContextMenu">
      <arg type="i" name="x" direction="in"/>
      <arg type="i" name="y" direction="in"/>
    </method>
    <method name="Scroll">
      <arg type="i" name="delta" direction="in"/>
      <arg type="s" name="orientation" direction="in"/>
    </method>
    <signal name="NewTitle"/>
    <signal name="NewIcon"/>
    <signal name="NewStatus">
      <arg type="s" name="status"/>
    </signal>
    <signal name="NewToolTip"/>
    <property name="Category" type="s" access="read"/>
    <property name="Id" type="s" access="read"/>
    <property name="Title" type="s" access="read"/>
    <property name="Status" type="s" access="read"/>
    <property name="WindowId" type="u" access="read"/>
    <property name="IconName" type="s" access="read"/>
    <property name="IconThemePath" type="s" access="read"/>
    <property name="OverlayIconName" type="s" access="read"/>
    <property name="AttentionIconName" type="s" access="read"/>
    <property name="ToolTip" type="(sa(iiay)ss)" access="read"/>
    <property name="Menu" type="o" access="read"/>
    <property name="ItemIsMenu" type="b" access="read"/>
  </interface>
</node>
)XML";

GDBusNodeInfo* get_sni_node_info() {
    static GDBusNodeInfo* info = g_dbus_node_info_new_for_xml(SNI_INTROSPECTION_XML, nullptr);
    return info;
}

} // namespace

std::string StatusNotifier::resolve_tray_icon_name() {
    GtkIconTheme* theme = gtk_icon_theme_get_default();
    if (theme) {
        const char* candidates[] = {
            "input-gaming-symbolic",
            "input-gaming",
            "applications-games-symbolic",
            "trayplay",
            "applications-games",
        };
        for (const char* name : candidates) {
            if (gtk_icon_theme_has_icon(theme, name)) {
                return name;
            }
        }
    }
    return "input-gaming-symbolic";
}

StatusNotifier::StatusNotifier(GDBusConnection* connection, Callbacks callbacks)
    : m_conn(connection),
      m_callbacks(std::move(callbacks)),
      m_cancellable(g_cancellable_new()),
      m_icon_name(resolve_tray_icon_name()) {
    if (m_conn) {
        g_object_ref(m_conn);
        register_dbusmenu();
        register_sni_object();

        m_watcher_id = g_bus_watch_name_on_connection(
            m_conn,
            WATCHER_BUS,
            G_BUS_NAME_WATCHER_FLAGS_NONE,
            +[](GDBusConnection*, const gchar*, const gchar*, gpointer data) {
                auto* self = static_cast<StatusNotifier*>(data);
                self->call_register_with_watcher();
            },
            +[](GDBusConnection*, const gchar*, gpointer data) {
                auto* self = static_cast<StatusNotifier*>(data);
                self->m_registered_with_watcher = false;
                self->enable_fallback_status_icon();
            },
            this,
            nullptr
        );

        // Give the session bus a brief moment for watcher discovery before enabling legacy fallback
        m_fallback_timer_id = g_timeout_add(350, +[](gpointer data) -> gboolean {
            auto* self = static_cast<StatusNotifier*>(data);
            self->m_fallback_timer_id = 0;
            if (!self->m_registered_with_watcher) {
                self->enable_fallback_status_icon();
            }
            return G_SOURCE_REMOVE;
        }, this);
    } else {
        enable_fallback_status_icon();
    }
}

StatusNotifier::~StatusNotifier() {
    *m_alive = false;
    if (m_cancellable) {
        g_cancellable_cancel(m_cancellable);
        g_object_unref(m_cancellable);
        m_cancellable = nullptr;
    }
    if (m_fallback_timer_id != 0) {
        g_source_remove(m_fallback_timer_id);
        m_fallback_timer_id = 0;
    }
    if (m_watcher_id != 0) {
        g_bus_unwatch_name(m_watcher_id);
        m_watcher_id = 0;
    }
    disable_fallback_status_icon();

#ifdef HAVE_DBUSMENU
    if (m_menu_server) {
        g_object_unref(m_menu_server);
        m_menu_server = nullptr;
        m_toggle_menu_item = nullptr;
    }
#endif

    if (m_conn) {
        if (m_sni_reg_id != 0) {
            g_dbus_connection_unregister_object(m_conn, m_sni_reg_id);
            m_sni_reg_id = 0;
        }
        g_object_unref(m_conn);
        m_conn = nullptr;
    }
}

void StatusNotifier::register_dbusmenu() {
#ifdef HAVE_DBUSMENU
    m_menu_server = dbusmenu_server_new(DBUSMENU_OBJECT_PATH);
    DbusmenuMenuitem* root = dbusmenu_menuitem_new();

    DbusmenuMenuitem* header_item = dbusmenu_menuitem_new();
    dbusmenu_menuitem_property_set(header_item, DBUSMENU_MENUITEM_PROP_LABEL, "TrayPlay Microconsole");
    dbusmenu_menuitem_property_set_bool(header_item, DBUSMENU_MENUITEM_PROP_ENABLED, FALSE);
    dbusmenu_menuitem_child_append(root, header_item);
    g_object_unref(header_item);

    m_toggle_menu_item = dbusmenu_menuitem_new();
    dbusmenu_menuitem_property_set(m_toggle_menu_item, DBUSMENU_MENUITEM_PROP_LABEL, "Open Screen");
    dbusmenu_menuitem_property_set(m_toggle_menu_item, DBUSMENU_MENUITEM_PROP_ICON_NAME, "input-gaming-symbolic");
    g_signal_connect(m_toggle_menu_item, DBUSMENU_MENUITEM_SIGNAL_ITEM_ACTIVATED,
                     G_CALLBACK(+[](DbusmenuMenuitem*, guint, gpointer data) {
                         auto* self = static_cast<StatusNotifier*>(data);
                         if (self->m_callbacks.on_open_screen) {
                             self->m_callbacks.on_open_screen();
                         }
                     }), this);
    dbusmenu_menuitem_child_append(root, m_toggle_menu_item);

    DbusmenuMenuitem* eject_item = dbusmenu_menuitem_new();
    dbusmenu_menuitem_property_set(eject_item, DBUSMENU_MENUITEM_PROP_LABEL, "Eject Cartridge");
    dbusmenu_menuitem_property_set(eject_item, DBUSMENU_MENUITEM_PROP_ICON_NAME, "media-eject-symbolic");
    g_signal_connect(eject_item, DBUSMENU_MENUITEM_SIGNAL_ITEM_ACTIVATED,
                     G_CALLBACK(+[](DbusmenuMenuitem*, guint, gpointer data) {
                         auto* self = static_cast<StatusNotifier*>(data);
                         if (self->m_callbacks.on_eject) {
                             self->m_callbacks.on_eject();
                         }
                     }), this);
    dbusmenu_menuitem_child_append(root, eject_item);
    g_object_unref(eject_item);

    DbusmenuMenuitem* sep_item = dbusmenu_menuitem_new();
    dbusmenu_menuitem_property_set(sep_item, DBUSMENU_MENUITEM_PROP_TYPE, "separator");
    dbusmenu_menuitem_child_append(root, sep_item);
    g_object_unref(sep_item);

    DbusmenuMenuitem* quit_item = dbusmenu_menuitem_new();
    dbusmenu_menuitem_property_set(quit_item, DBUSMENU_MENUITEM_PROP_LABEL, "Quit");
    dbusmenu_menuitem_property_set(quit_item, DBUSMENU_MENUITEM_PROP_ICON_NAME, "application-exit-symbolic");
    g_signal_connect(quit_item, DBUSMENU_MENUITEM_SIGNAL_ITEM_ACTIVATED,
                     G_CALLBACK(+[](DbusmenuMenuitem*, guint, gpointer data) {
                         auto* self = static_cast<StatusNotifier*>(data);
                         if (self->m_callbacks.on_quit) {
                             self->m_callbacks.on_quit();
                         }
                     }), this);
    dbusmenu_menuitem_child_append(root, quit_item);
    g_object_unref(quit_item);

    dbusmenu_server_set_root(m_menu_server, root);
    g_object_unref(root);
#endif
}

void StatusNotifier::register_sni_object() {
    GDBusNodeInfo* node = get_sni_node_info();
    if (!node || !m_conn) return;

    GDBusInterfaceInfo* iface = g_dbus_node_info_lookup_interface(node, SNI_INTERFACE);
    if (!iface) return;

    static const GDBusInterfaceVTable vtable = {
        on_method_call,
        on_get_property,
        nullptr,
        {nullptr}
    };

    GError* err = nullptr;
    m_sni_reg_id = g_dbus_connection_register_object(
        m_conn,
        SNI_OBJECT_PATH,
        iface,
        &vtable,
        this,
        nullptr,
        &err
    );
    if (err) {
        g_warning("[TrayPlay] Failed to register SNI object: %s", err->message);
        g_error_free(err);
    }
}

void StatusNotifier::call_register_with_watcher() {
    if (!m_conn || m_sni_reg_id == 0) return;

    struct WatchCtx {
        StatusNotifier* self;
        std::weak_ptr<bool> weak;
    };

    // First verify whether an SNI host (such as xfce4-panel systray) is registered
    auto* ctx1 = new WatchCtx{this, m_alive};
    g_dbus_connection_call(
        m_conn,
        WATCHER_BUS,
        WATCHER_PATH,
        "org.freedesktop.DBus.Properties",
        "Get",
        g_variant_new("(ss)", WATCHER_IFACE, "IsStatusNotifierHostRegistered"),
        G_VARIANT_TYPE("(v)"),
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        m_cancellable,
        +[](GObject* source, GAsyncResult* res, gpointer data) {
            auto* c1 = static_cast<WatchCtx*>(data);
            GError* err = nullptr;
            GVariant* reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), res, &err);
            auto alive = c1->weak.lock();
            StatusNotifier* self = c1->self;
            delete c1;

            if (!alive || !*alive) {
                if (reply) g_variant_unref(reply);
                if (err) g_error_free(err);
                return;
            }

            bool host_present = false;
            if (reply) {
                GVariant* inner = nullptr;
                g_variant_get(reply, "(v)", &inner);
                if (inner && g_variant_is_of_type(inner, G_VARIANT_TYPE_BOOLEAN)) {
                    host_present = g_variant_get_boolean(inner) == TRUE;
                }
                if (inner) g_variant_unref(inner);
                g_variant_unref(reply);
            } else if (err) {
                g_error_free(err);
            }

            if (!host_present) {
                self->m_registered_with_watcher = false;
                self->enable_fallback_status_icon();
                return;
            }

            auto* ctx2 = new WatchCtx{self, self->m_alive};
            g_dbus_connection_call(
                self->m_conn,
                WATCHER_BUS,
                WATCHER_PATH,
                WATCHER_IFACE,
                "RegisterStatusNotifierItem",
                g_variant_new("(s)", SNI_OBJECT_PATH),
                nullptr,
                G_DBUS_CALL_FLAGS_NONE,
                -1,
                self->m_cancellable,
                +[](GObject* src2, GAsyncResult* res2, gpointer d2) {
                    auto* c2 = static_cast<WatchCtx*>(d2);
                    GError* err2 = nullptr;
                    GVariant* r2 = g_dbus_connection_call_finish(G_DBUS_CONNECTION(src2), res2, &err2);
                    auto alive2 = c2->weak.lock();
                    StatusNotifier* s2 = c2->self;
                    delete c2;

                    if (!alive2 || !*alive2) {
                        if (r2) g_variant_unref(r2);
                        if (err2) g_error_free(err2);
                        return;
                    }

                    if (r2) {
                        g_variant_unref(r2);
                        s2->m_registered_with_watcher = true;
                        s2->disable_fallback_status_icon();
                    } else {
                        if (err2) g_error_free(err2);
                        s2->m_registered_with_watcher = false;
                        s2->enable_fallback_status_icon();
                    }
                },
                ctx2
            );
        },
        ctx1
    );
}

GVariant* StatusNotifier::on_get_property(GDBusConnection*,
                                          const gchar*,
                                          const gchar*,
                                          const gchar*,
                                          const gchar* property_name,
                                          GError**,
                                          gpointer user_data) {
    auto* self = static_cast<StatusNotifier*>(user_data);

    if (g_strcmp0(property_name, "Category") == 0) {
        return g_variant_new_string("ApplicationStatus");
    }
    if (g_strcmp0(property_name, "Id") == 0) {
        return g_variant_new_string("trayplay");
    }
    if (g_strcmp0(property_name, "Title") == 0) {
        return g_variant_new_string("TrayPlay");
    }
    if (g_strcmp0(property_name, "Status") == 0) {
        return g_variant_new_string("Active");
    }
    if (g_strcmp0(property_name, "WindowId") == 0) {
        return g_variant_new_uint32(0);
    }
    if (g_strcmp0(property_name, "IconName") == 0) {
        return g_variant_new_string(self->m_icon_name.c_str());
    }
    if (g_strcmp0(property_name, "IconThemePath") == 0) {
        return g_variant_new_string("");
    }
    if (g_strcmp0(property_name, "OverlayIconName") == 0) {
        return g_variant_new_string("");
    }
    if (g_strcmp0(property_name, "AttentionIconName") == 0) {
        return g_variant_new_string("");
    }
    if (g_strcmp0(property_name, "ToolTip") == 0) {
        GVariantBuilder pix_builder;
        g_variant_builder_init(&pix_builder, G_VARIANT_TYPE("a(iiay)"));
        return g_variant_new(
            "(s@a(iiay)ss)",
            self->m_icon_name.c_str(),
            g_variant_builder_end(&pix_builder),
            self->m_tooltip_title.c_str(),
            self->m_tooltip_body.c_str()
        );
    }
    if (g_strcmp0(property_name, "Menu") == 0) {
#ifdef HAVE_DBUSMENU
        return g_variant_new_object_path(DBUSMENU_OBJECT_PATH);
#else
        return g_variant_new_object_path("/");
#endif
    }
    if (g_strcmp0(property_name, "ItemIsMenu") == 0) {
        return g_variant_new_boolean(FALSE);
    }

    return nullptr;
}

void StatusNotifier::on_method_call(GDBusConnection*,
                                    const gchar*,
                                    const gchar*,
                                    const gchar*,
                                    const gchar* method_name,
                                    GVariant* parameters,
                                    GDBusMethodInvocation* invocation,
                                    gpointer user_data) {
    auto* self = static_cast<StatusNotifier*>(user_data);

    if (g_strcmp0(method_name, "Activate") == 0 || g_strcmp0(method_name, "SecondaryActivate") == 0) {
        gint32 x = 0;
        gint32 y = 0;
        g_variant_get(parameters, "(ii)", &x, &y);
        if (self->m_callbacks.on_activate) {
            self->m_callbacks.on_activate(x, y);
        }
        g_dbus_method_invocation_return_value(invocation, nullptr);
        return;
    }

    if (g_strcmp0(method_name, "ContextMenu") == 0) {
        self->show_fallback_popup_menu(3, GDK_CURRENT_TIME);
        g_dbus_method_invocation_return_value(invocation, nullptr);
        return;
    }

    if (g_strcmp0(method_name, "Scroll") == 0) {
        g_dbus_method_invocation_return_value(invocation, nullptr);
        return;
    }

    g_dbus_method_invocation_return_dbus_error(
        invocation,
        "org.freedesktop.DBus.Error.UnknownMethod",
        "Unknown method"
    );
}

void StatusNotifier::emit_sni_signal(const char* signal_name) {
    if (!m_conn || m_sni_reg_id == 0) return;
    g_dbus_connection_emit_signal(
        m_conn,
        nullptr,
        SNI_OBJECT_PATH,
        SNI_INTERFACE,
        signal_name,
        nullptr,
        nullptr
    );
}

void StatusNotifier::set_status_info(DaemonState state, const std::string& title) {
    switch (state) {
        case DaemonState::RUNNING:
            m_tooltip_body = "Playing: " + (title.empty() ? "Cartridge" : title);
            break;
        case DaemonState::PAUSED:
            m_tooltip_body = "Paused: " + (title.empty() ? "Cartridge" : title);
            break;
        case DaemonState::CRASHED:
            m_tooltip_body = "Error: " + (title.empty() ? "Cartridge Crashed" : title);
            break;
        case DaemonState::STOPPED:
        default:
            m_tooltip_body = "Waiting for cartridge";
            break;
    }

    emit_sni_signal("NewToolTip");

    if (m_fallback_icon) {
        G_GNUC_BEGIN_IGNORE_DEPRECATIONS
        const std::string full_tip = m_tooltip_title + " — " + m_tooltip_body;
        gtk_status_icon_set_tooltip_text(m_fallback_icon, full_tip.c_str());
        G_GNUC_END_IGNORE_DEPRECATIONS
    }
}

void StatusNotifier::set_popup_visible(bool visible) {
    m_popup_visible = visible;
#ifdef HAVE_DBUSMENU
    if (m_toggle_menu_item) {
        dbusmenu_menuitem_property_set(
            m_toggle_menu_item,
            DBUSMENU_MENUITEM_PROP_LABEL,
            visible ? "Hide Screen" : "Open Screen"
        );
    }
#endif
}

void StatusNotifier::enable_fallback_status_icon() {
    if (m_fallback_icon) return;

    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    m_fallback_icon = gtk_status_icon_new_from_icon_name(m_icon_name.c_str());
    const std::string full_tip = m_tooltip_title + " — " + m_tooltip_body;
    gtk_status_icon_set_tooltip_text(m_fallback_icon, full_tip.c_str());
    gtk_status_icon_set_visible(m_fallback_icon, TRUE);

    g_signal_connect(m_fallback_icon, "activate", G_CALLBACK(+[](GtkStatusIcon* icon, gpointer data) {
        auto* self = static_cast<StatusNotifier*>(data);
        GdkRectangle area {};
        GdkScreen* screen = nullptr;
        GtkOrientation orient = GTK_ORIENTATION_HORIZONTAL;
        int ax = 0;
        int ay = 0;
        if (gtk_status_icon_get_geometry(icon, &screen, &area, &orient)) {
            ax = area.x + area.width / 2;
            ay = area.y + area.height / 2;
        }
        if (self->m_callbacks.on_activate) {
            self->m_callbacks.on_activate(ax, ay);
        }
    }), this);

    g_signal_connect(m_fallback_icon, "popup-menu", G_CALLBACK(+[](GtkStatusIcon*, guint button, guint32 activate_time, gpointer data) {
        static_cast<StatusNotifier*>(data)->show_fallback_popup_menu(button, activate_time);
    }), this);
    G_GNUC_END_IGNORE_DEPRECATIONS
}

void StatusNotifier::disable_fallback_status_icon() {
    if (!m_fallback_icon) return;
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    gtk_status_icon_set_visible(m_fallback_icon, FALSE);
    g_object_unref(m_fallback_icon);
    G_GNUC_END_IGNORE_DEPRECATIONS
    m_fallback_icon = nullptr;
}

void StatusNotifier::show_fallback_popup_menu(guint button, guint32 activate_time) {
    GtkWidget* menu = gtk_menu_new();

    GtkWidget* title_item = gtk_menu_item_new_with_label("TrayPlay Microconsole");
    gtk_widget_set_sensitive(title_item, FALSE);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), title_item);

    GtkWidget* open_item = gtk_menu_item_new_with_label(m_popup_visible ? "Hide Screen" : "Open Screen");
    g_signal_connect(open_item, "activate", G_CALLBACK(+[](GtkMenuItem*, gpointer data) {
        auto* self = static_cast<StatusNotifier*>(data);
        if (self->m_callbacks.on_open_screen) {
            self->m_callbacks.on_open_screen();
        }
    }), this);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), open_item);

    GtkWidget* eject_item = gtk_menu_item_new_with_label("Eject Cartridge");
    g_signal_connect(eject_item, "activate", G_CALLBACK(+[](GtkMenuItem*, gpointer data) {
        auto* self = static_cast<StatusNotifier*>(data);
        if (self->m_callbacks.on_eject) {
            self->m_callbacks.on_eject();
        }
    }), this);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), eject_item);

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());

    GtkWidget* quit_item = gtk_menu_item_new_with_label("Quit");
    g_signal_connect(quit_item, "activate", G_CALLBACK(+[](GtkMenuItem*, gpointer data) {
        auto* self = static_cast<StatusNotifier*>(data);
        if (self->m_callbacks.on_quit) {
            self->m_callbacks.on_quit();
        }
    }), this);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), quit_item);

    gtk_widget_show_all(menu);
    g_signal_connect(menu, "selection-done", G_CALLBACK(+[](GtkMenuShell* shell, gpointer) {
        gtk_widget_destroy(GTK_WIDGET(shell));
    }), nullptr);

    if (m_fallback_icon) {
        G_GNUC_BEGIN_IGNORE_DEPRECATIONS
        gtk_menu_popup(GTK_MENU(menu), nullptr, nullptr, gtk_status_icon_position_menu, m_fallback_icon, button, activate_time);
        G_GNUC_END_IGNORE_DEPRECATIONS
    } else {
        gtk_menu_popup_at_pointer(GTK_MENU(menu), nullptr);
    }
}

} // namespace trayplay::xfce
