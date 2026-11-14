#include "popup_window.h"
#include "status_notifier.h"
#include "trayplay_view.h"

#include <glib-unix.h>
#include <gtk/gtk.h>

#include <csignal>
#include <memory>

using namespace trayplay::xfce;

namespace {

struct AppContext {
    GtkApplication* app = nullptr;
    std::unique_ptr<TrayPlayView> view;
    std::unique_ptr<PopupWindow> popup;
    std::unique_ptr<StatusNotifier> notifier;
    bool initial_activate_done = false;
    bool open_on_start = false;
    bool quitting = false;
};

void quit_application(AppContext* ctx) {
    if (!ctx || ctx->quitting) return;
    ctx->quitting = true;

    if (ctx->popup) {
        ctx->popup->hide();
    }
    if (ctx->view) {
        ctx->view->shutdown_daemon();
    }

    // Defer destroying GTK widgets and DbusmenuServer out of active signal emission stacks
    g_idle_add(+[](gpointer data) -> gboolean {
        auto* c = static_cast<AppContext*>(data);
        c->notifier.reset();
        c->popup.reset();
        c->view.reset();

        if (c->app) {
            g_application_release(G_APPLICATION(c->app));
            g_application_quit(G_APPLICATION(c->app));
        }
        return G_SOURCE_REMOVE;
    }, ctx);
}

void on_startup(GApplication* gapp, gpointer user_data) {
    auto* ctx = static_cast<AppContext*>(user_data);
    ctx->app = GTK_APPLICATION(gapp);

    // Keep application running in system tray even while popup window is hidden
    g_application_hold(gapp);

    TrayPlayView::Callbacks view_cbs;
    view_cbs.on_quit_requested = [ctx]() {
        quit_application(ctx);
    };
    view_cbs.on_hide_requested = [ctx]() {
        if (ctx->popup) {
            ctx->popup->hide();
        }
    };
    view_cbs.on_modal_dialog_state = [ctx](bool active) {
        if (ctx->popup) {
            ctx->popup->set_modal_dialog_active(active);
        }
    };
    view_cbs.on_status_changed = [ctx](DaemonState state, const std::string& title) {
        if (ctx->notifier) {
            ctx->notifier->set_status_info(state, title);
        }
    };

    ctx->view = std::make_unique<TrayPlayView>(std::move(view_cbs));
    ctx->popup = std::make_unique<PopupWindow>(*ctx->view);
    ctx->popup->set_on_visibility_changed([ctx](bool visible) {
        if (ctx->notifier) {
            ctx->notifier->set_popup_visible(visible);
        }
    });

    StatusNotifier::Callbacks sni_cbs;
    sni_cbs.on_activate = [ctx](int x, int y) {
        if (ctx->popup) {
            ctx->popup->toggle_near(x, y);
        }
    };
    sni_cbs.on_open_screen = [ctx]() {
        if (ctx->popup) {
            ctx->popup->toggle_near(0, 0);
        }
    };
    sni_cbs.on_eject = [ctx]() {
        if (ctx->view) {
            ctx->view->eject_cartridge();
        }
    };
    sni_cbs.on_quit = [ctx]() {
        quit_application(ctx);
    };

    GDBusConnection* conn = g_application_get_dbus_connection(gapp);
    ctx->notifier = std::make_unique<StatusNotifier>(conn, std::move(sni_cbs));

    g_unix_signal_add(SIGINT, +[](gpointer data) -> gboolean {
        quit_application(static_cast<AppContext*>(data));
        return G_SOURCE_REMOVE;
    }, ctx);

    g_unix_signal_add(SIGTERM, +[](gpointer data) -> gboolean {
        quit_application(static_cast<AppContext*>(data));
        return G_SOURCE_REMOVE;
    }, ctx);
}

void on_activate(GApplication*, gpointer user_data) {
    auto* ctx = static_cast<AppContext*>(user_data);
    if (!ctx->initial_activate_done) {
        ctx->initial_activate_done = true;
        if (ctx->open_on_start && ctx->popup) {
            ctx->popup->show_near(0, 0);
        }
        return;
    }

    // Subsequent launches while already residing in the tray toggle the console screen
    if (ctx->popup) {
        ctx->popup->toggle_near(0, 0);
    }
}

void on_shutdown(GApplication*, gpointer user_data) {
    auto* ctx = static_cast<AppContext*>(user_data);
    ctx->notifier.reset();
    ctx->popup.reset();
    ctx->view.reset();
}

} // namespace

int main(int argc, char* argv[]) {
    std::signal(SIGPIPE, SIG_IGN);

    AppContext ctx;
    for (int i = 1; i < argc; ++i) {
        if (g_strcmp0(argv[i], "--show") == 0) {
            ctx.open_on_start = true;
        }
    }

#if GLIB_CHECK_VERSION(2, 74, 0)
    const GApplicationFlags flags = G_APPLICATION_DEFAULT_FLAGS;
#else
    const GApplicationFlags flags = G_APPLICATION_FLAGS_NONE;
#endif

    GtkApplication* app = gtk_application_new("io.github.threetwofourfive.trayplay.xfce", flags);
    g_signal_connect(app, "startup", G_CALLBACK(on_startup), &ctx);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), &ctx);
    g_signal_connect(app, "shutdown", G_CALLBACK(on_shutdown), &ctx);

    const int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
