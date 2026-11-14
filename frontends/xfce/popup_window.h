#pragma once

#include "trayplay_view.h"

#include <gtk/gtk.h>

#include <cstdint>
#include <functional>

namespace trayplay::xfce {

class PopupWindow {
public:
    explicit PopupWindow(TrayPlayView& view, bool enable_layer_shell = true);
    ~PopupWindow();

    PopupWindow(const PopupWindow&) = delete;
    PopupWindow& operator=(const PopupWindow&) = delete;

    GtkWindow* gtk_window() const { return m_window; }
    bool is_visible() const;
    bool was_recently_hidden_by_focus_out() const;

    void show_near(int anchor_x, int anchor_y);
    void show_at_exact(int x, int y);
    void notify_shown_externally();
    void toggle_near(int anchor_x, int anchor_y);
    void hide();

    void set_modal_dialog_active(bool active);
    void set_on_visibility_changed(std::function<void(bool visible)> cb) {
        m_on_visibility_changed = std::move(cb);
    }

private:
    void compute_popup_position(int anchor_x, int anchor_y, int& out_x, int& out_y) const;
    void apply_visual_for_screen(GdkScreen* screen);
    void present_window();

    TrayPlayView& m_view;
    GtkWindow* m_window = nullptr;
    bool m_is_wayland_layer = false;
    bool m_modal_dialog_active = false;
    bool m_has_last_anchor = false;
    int m_last_anchor_x = 0;
    int m_last_anchor_y = 0;
    gint64 m_last_focus_hide_us = 0;
    std::function<void(bool visible)> m_on_visibility_changed;
};

} // namespace trayplay::xfce
