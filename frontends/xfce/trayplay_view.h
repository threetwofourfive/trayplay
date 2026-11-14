#pragma once

#include "cartridge_browser.h"
#include "ipc_client.h"

#include <cairo.h>
#include <gtk/gtk.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_set>

namespace trayplay::xfce {

class TrayPlayView {
public:
    struct Callbacks {
        std::function<void()> on_quit_requested;
        std::function<void()> on_hide_requested;
        std::function<void(bool active)> on_modal_dialog_state;
        std::function<void(DaemonState state, const std::string& title)> on_status_changed;
    };

    explicit TrayPlayView(Callbacks callbacks);
    ~TrayPlayView();

    TrayPlayView(const TrayPlayView&) = delete;
    TrayPlayView& operator=(const TrayPlayView&) = delete;

    static void ensure_css_loaded();

    GtkWidget* widget() const { return m_root; }
    void set_parent_window(GtkWindow* parent_window);

    void on_popup_shown();
    void on_popup_hidden();

    void eject_cartridge();
    void shutdown_daemon();

    bool handle_key_press(const GdkEventKey* event);
    bool handle_key_release(const GdkEventKey* event);

private:
    void start_polling();
    void stop_polling();
    void poll_frame();

    void open_browser();
    void close_browser(bool refresh_game_view = true);
    void load_cartridge(const std::string& path);
    void schedule_load_retry(const std::string& path, uint64_t request_id, int attempts_left);

    void set_waiting_state();
    void set_error_state(const std::string& msg = {});
    void set_paused_state(const std::string& title);
    void set_active_state(const std::string& title, float fps);
    void set_title_text(const std::string& text);
    void set_screen_page(const char* page_name);
    void set_placeholder(const char* title, const std::string& status, bool is_error);
    void update_status_badge(const char* text, const char* css_class);
    void notify_status(DaemonState state, const std::string& title);

    void release_all_buttons();
    void focus_canvas();

    static gboolean on_canvas_draw(GtkWidget* widget, cairo_t* cr, gpointer data);
    static const char* map_key_to_button(guint keyval);

    Callbacks m_callbacks;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);

    IpcClient m_ipc;
    std::unique_ptr<CartridgeBrowser> m_browser;

    GtkWindow* m_parent_window = nullptr;
    GtkWidget* m_root = nullptr;
    GtkWidget* m_title_label = nullptr;
    GtkWidget* m_status_badge = nullptr;
    GtkWidget* m_main_stack = nullptr;
    GtkWidget* m_screen_stack = nullptr;
    GtkWidget* m_canvas = nullptr;
    GtkWidget* m_placeholder_title = nullptr;
    GtkWidget* m_placeholder_status = nullptr;

    cairo_surface_t* m_surface = nullptr;
    bool m_has_valid_frame = false;
    uint64_t m_last_frame_index = UINT64_MAX;
    int m_stale_frame_ticks = 0;
    guint m_poll_timer_id = 0;
    bool m_is_visible = false;
    bool m_is_browser_active = false;
    bool m_explicit_error = false;
    DaemonState m_current_state = DaemonState::STOPPED;
    std::string m_last_notified_title;
    DaemonState m_last_notified_state = DaemonState::STOPPED;
    bool m_status_notified_once = false;

    uint64_t m_load_request_id = 0;

    // UI state caches prevent 60 FPS redundant GTK CSS/Pango layout recalculations
    std::string m_cached_title_text = "TRAYPLAY";
    std::string m_cached_badge_text = "● WAITING";
    std::string m_cached_badge_class = "status-waiting";
    std::string m_cached_screen_page = "placeholder";
    std::string m_cached_placeholder_title = "▶ TRAYPLAY ◀";
    std::string m_cached_placeholder_status = "[ NO CARTRIDGE LOADED ]";
    bool m_cached_placeholder_error = false;
    int m_cached_fps = -1;

    std::unordered_set<std::string> m_pressed_buttons;
};

} // namespace trayplay::xfce
