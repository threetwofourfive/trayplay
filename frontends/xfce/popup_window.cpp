#include "popup_window.h"

#ifdef HAVE_GTK_LAYER_SHELL
#include <gtk-layer-shell/gtk-layer-shell.h>
#endif

#ifdef GDK_WINDOWING_WAYLAND
#include <gdk/gdkwayland.h>
#endif

#include <algorithm>

namespace trayplay::xfce {

namespace {

bool is_wayland_display() {
#ifdef GDK_WINDOWING_WAYLAND
    GdkDisplay* display = gdk_display_get_default();
    return display && GDK_IS_WAYLAND_DISPLAY(display);
#else
    return false;
#endif
}

} // namespace

PopupWindow::PopupWindow(TrayPlayView& view, bool enable_layer_shell)
    : m_view(view) {
    m_window = GTK_WINDOW(gtk_window_new(GTK_WINDOW_TOPLEVEL));
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(m_window)), "trayplay-popup-window");

    gtk_window_set_title(m_window, "TrayPlay");
    gtk_window_set_decorated(m_window, FALSE);
    gtk_window_set_resizable(m_window, FALSE);
    gtk_window_set_skip_taskbar_hint(m_window, TRUE);
    gtk_window_set_skip_pager_hint(m_window, TRUE);
    gtk_window_set_keep_above(m_window, TRUE);
    gtk_window_set_accept_focus(m_window, TRUE);
    gtk_window_stick(m_window);
    gtk_window_set_type_hint(m_window, GDK_WINDOW_TYPE_HINT_POPUP_MENU);

    apply_visual_for_screen(gtk_widget_get_screen(GTK_WIDGET(m_window)));
    g_signal_connect(m_window, "screen-changed", G_CALLBACK(+[](GtkWidget* w, GdkScreen*, gpointer data) {
        static_cast<PopupWindow*>(data)->apply_visual_for_screen(gtk_widget_get_screen(w));
    }), this);

    g_signal_connect(m_window, "draw", G_CALLBACK(+[](GtkWidget* w, cairo_t* cr, gpointer) -> gboolean {
        GdkScreen* screen = gtk_widget_get_screen(w);
        if (screen && gdk_screen_is_composited(screen)) {
            cairo_save(cr);
            cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
            cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.0);
            cairo_paint(cr);
            cairo_restore(cr);
        }
        return FALSE;
    }), nullptr);

#ifdef HAVE_GTK_LAYER_SHELL
    if (enable_layer_shell && is_wayland_display() && gtk_layer_is_supported()) {
        gtk_layer_init_for_window(m_window);
        gtk_layer_set_namespace(m_window, "trayplay");
        gtk_layer_set_layer(m_window, GTK_LAYER_SHELL_LAYER_OVERLAY);
        gtk_layer_set_keyboard_mode(m_window, GTK_LAYER_SHELL_KEYBOARD_MODE_ON_DEMAND);
        gtk_layer_set_anchor(m_window, GTK_LAYER_SHELL_EDGE_TOP, TRUE);
        gtk_layer_set_anchor(m_window, GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
        gtk_layer_set_margin(m_window, GTK_LAYER_SHELL_EDGE_TOP, 6);
        gtk_layer_set_margin(m_window, GTK_LAYER_SHELL_EDGE_RIGHT, 6);
        m_is_wayland_layer = true;
    }
#endif

    gtk_container_add(GTK_CONTAINER(m_window), m_view.widget());
    m_view.set_parent_window(m_window);

    gtk_widget_add_events(GTK_WIDGET(m_window), GDK_KEY_PRESS_MASK | GDK_KEY_RELEASE_MASK | GDK_FOCUS_CHANGE_MASK);

    g_signal_connect(m_window, "key-press-event", G_CALLBACK(+[](GtkWidget*, GdkEventKey* ev, gpointer data) -> gboolean {
        return static_cast<PopupWindow*>(data)->m_view.handle_key_press(ev) ? GDK_EVENT_STOP : GDK_EVENT_PROPAGATE;
    }), this);

    g_signal_connect(m_window, "key-release-event", G_CALLBACK(+[](GtkWidget*, GdkEventKey* ev, gpointer data) -> gboolean {
        return static_cast<PopupWindow*>(data)->m_view.handle_key_release(ev) ? GDK_EVENT_STOP : GDK_EVENT_PROPAGATE;
    }), this);

    g_signal_connect(m_window, "focus-out-event", G_CALLBACK(+[](GtkWidget*, GdkEventFocus*, gpointer data) -> gboolean {
        auto* self = static_cast<PopupWindow*>(data);
        if (!self->m_modal_dialog_active && self->is_visible()) {
            self->m_last_focus_hide_us = g_get_monotonic_time();
            self->hide();
        }
        return GDK_EVENT_PROPAGATE;
    }), this);

    g_signal_connect(m_window, "delete-event", G_CALLBACK(+[](GtkWidget*, GdkEvent*, gpointer data) -> gboolean {
        static_cast<PopupWindow*>(data)->hide();
        return TRUE;
    }), this);
}

PopupWindow::~PopupWindow() {
    if (m_window) {
        gtk_widget_destroy(GTK_WIDGET(m_window));
        m_window = nullptr;
    }
}

void PopupWindow::apply_visual_for_screen(GdkScreen* screen) {
    if (!screen) return;
    GtkStyleContext* ctx = gtk_widget_get_style_context(m_view.widget());
    if (gdk_screen_is_composited(screen)) {
        if (GdkVisual* rgba = gdk_screen_get_rgba_visual(screen)) {
            gtk_widget_set_visual(GTK_WIDGET(m_window), rgba);
            gtk_widget_set_app_paintable(GTK_WIDGET(m_window), TRUE);
            gtk_style_context_remove_class(ctx, "non-composited");
            return;
        }
    }
    gtk_widget_set_app_paintable(GTK_WIDGET(m_window), FALSE);
    gtk_style_context_add_class(ctx, "non-composited");
}

bool PopupWindow::is_visible() const {
    return m_window && gtk_widget_get_visible(GTK_WIDGET(m_window));
}

bool PopupWindow::was_recently_hidden_by_focus_out() const {
    const gint64 elapsed_us = g_get_monotonic_time() - m_last_focus_hide_us;
    return elapsed_us >= 0 && elapsed_us < 250000;
}

void PopupWindow::compute_popup_position(int anchor_x, int anchor_y, int& out_x, int& out_y) const {
    GtkRequisition min_req {};
    GtkRequisition nat_req {};
    gtk_widget_get_preferred_size(GTK_WIDGET(m_window), &min_req, &nat_req);
    const int win_w = std::max(nat_req.width, 450);
    const int win_h = std::max(nat_req.height, 320);

    GdkDisplay* display = gdk_display_get_default();
    if (!display) {
        out_x = std::max(0, anchor_x - win_w / 2);
        out_y = std::max(0, anchor_y);
        return;
    }

    const bool has_valid_point = (anchor_x > 0 || anchor_y > 0);
    GdkMonitor* monitor = has_valid_point
        ? gdk_display_get_monitor_at_point(display, anchor_x, anchor_y)
        : gdk_display_get_primary_monitor(display);
    if (!monitor) {
        monitor = gdk_display_get_monitor(display, 0);
    }
    if (!monitor) {
        out_x = 0;
        out_y = 0;
        return;
    }

    GdkRectangle geom {};
    GdkRectangle work {};
    gdk_monitor_get_geometry(monitor, &geom);
    gdk_monitor_get_workarea(monitor, &work);

    const int margin = 6;
    if (!has_valid_point) {
        const bool has_top_panel = work.y > geom.y;
        const bool has_bottom_panel = (work.y + work.height) < (geom.y + geom.height);
        const bool has_left_panel = work.x > geom.x;

        out_x = has_left_panel
            ? (work.x + margin)
            : (work.x + work.width - win_w - margin);
        out_y = (has_bottom_panel && !has_top_panel)
            ? (work.y + work.height - win_h - margin)
            : (work.y + margin);
        return;
    }

    int x = anchor_x - win_w / 2;
    int y = work.y + margin;

    const bool has_top_panel = work.y > geom.y;
    const bool has_bottom_panel = (work.y + work.height) < (geom.y + geom.height);
    const bool has_left_panel = work.x > geom.x;
    const bool has_right_panel = (work.x + work.width) < (geom.x + geom.width);

    if (has_top_panel && anchor_y <= work.y + 16) {
        x = anchor_x - win_w / 2;
        y = work.y + margin;
    } else if (has_bottom_panel && anchor_y >= work.y + work.height - 16) {
        x = anchor_x - win_w / 2;
        y = work.y + work.height - win_h - margin;
    } else if (has_left_panel && anchor_x <= work.x + 16) {
        x = work.x + margin;
        y = anchor_y - win_h / 2;
    } else if (has_right_panel && anchor_x >= work.x + work.width - 16) {
        x = work.x + work.width - win_w - margin;
        y = anchor_y - win_h / 2;
    } else {
        // Fallback for autohide panels where workarea equals monitor geometry
        x = anchor_x - win_w / 2;
        if (anchor_y < geom.y + geom.height / 2) {
            y = std::max(geom.y + margin, anchor_y + 18);
        } else {
            y = anchor_y - win_h - 18;
        }
    }

    const int min_x = work.x + margin;
    const int max_x = std::max(min_x, work.x + work.width - win_w - margin);
    const int min_y = work.y + margin;
    const int max_y = std::max(min_y, work.y + work.height - win_h - margin);

    out_x = std::clamp(x, min_x, max_x);
    out_y = std::clamp(y, min_y, max_y);
}

void PopupWindow::show_near(int anchor_x, int anchor_y) {
    if (anchor_x > 0 || anchor_y > 0) {
        m_last_anchor_x = anchor_x;
        m_last_anchor_y = anchor_y;
        m_has_last_anchor = true;
    } else if (m_has_last_anchor) {
        anchor_x = m_last_anchor_x;
        anchor_y = m_last_anchor_y;
    }

    if (!m_is_wayland_layer) {
        int x = 0;
        int y = 0;
        compute_popup_position(anchor_x, anchor_y, x, y);
        gtk_window_move(m_window, x, y);
        present_window();
        gtk_window_move(m_window, x, y);
    } else {
        present_window();
    }
}

void PopupWindow::show_at_exact(int x, int y) {
    if (!m_is_wayland_layer) {
        gtk_window_move(m_window, x, y);
        present_window();
        gtk_window_move(m_window, x, y);
    } else {
        present_window();
    }
}

void PopupWindow::present_window() {
    gtk_widget_show_all(GTK_WIDGET(m_window));
    gtk_window_present_with_time(m_window, GDK_CURRENT_TIME);
    notify_shown_externally();
}

void PopupWindow::notify_shown_externally() {
    m_view.on_popup_shown();
    if (m_on_visibility_changed) {
        m_on_visibility_changed(true);
    }
}

void PopupWindow::toggle_near(int anchor_x, int anchor_y) {
    if (is_visible()) {
        hide();
        return;
    }

    // Prevent immediate re-open when clicking the tray icon dismissed the popup via focus-out
    if (was_recently_hidden_by_focus_out()) {
        return;
    }

    show_near(anchor_x, anchor_y);
}

void PopupWindow::hide() {
    if (!is_visible()) return;
    gtk_widget_hide(GTK_WIDGET(m_window));
    m_view.on_popup_hidden();
    if (m_on_visibility_changed) {
        m_on_visibility_changed(false);
    }
}

void PopupWindow::set_modal_dialog_active(bool active) {
    m_modal_dialog_active = active;
    if (!active && is_visible()) {
        gtk_window_present_with_time(m_window, GDK_CURRENT_TIME);
    }
}

} // namespace trayplay::xfce
