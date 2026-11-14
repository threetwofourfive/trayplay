#include "trayplay_view.h"
#include "generated_style.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace trayplay::xfce {

namespace {

void add_css_class(GtkWidget* widget, const char* cls) {
    gtk_style_context_add_class(gtk_widget_get_style_context(widget), cls);
}

void remove_css_class(GtkWidget* widget, const char* cls) {
    gtk_style_context_remove_class(gtk_widget_get_style_context(widget), cls);
}

std::string to_upper_ascii(const std::string& s) {
    std::string out = s;
    for (char& c : out) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return out;
}

} // namespace

void TrayPlayView::ensure_css_loaded() {
    static bool loaded = false;
    if (loaded) return;
    loaded = true;

    GtkCssProvider* provider = gtk_css_provider_new();
    GError* err = nullptr;
    if (!gtk_css_provider_load_from_data(provider, TRAYPLAY_XFCE_STYLE_CSS, -1, &err)) {
        if (err) {
            g_warning("[TrayPlay] Failed to parse CSS: %s", err->message);
            g_error_free(err);
        }
    }
    if (GdkScreen* screen = gdk_screen_get_default()) {
        gtk_style_context_add_provider_for_screen(
            screen,
            GTK_STYLE_PROVIDER(provider),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
        );
    }
    g_object_unref(provider);
}

TrayPlayView::TrayPlayView(Callbacks callbacks)
    : m_callbacks(std::move(callbacks)) {
    ensure_css_loaded();

    m_surface = cairo_image_surface_create(
        CAIRO_FORMAT_ARGB32,
        IpcClient::VIRTUAL_WIDTH,
        IpcClient::VIRTUAL_HEIGHT
    );

    m_root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    add_css_class(m_root, "trayplay-popup-frame");

    // 1. Header Bar
    GtkWidget* header_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    add_css_class(header_box, "trayplay-header");

    GtkWidget* title_icon = gtk_image_new_from_icon_name("input-gaming-symbolic", GTK_ICON_SIZE_MENU);
    gtk_box_pack_start(GTK_BOX(header_box), title_icon, FALSE, FALSE, 0);

    m_title_label = gtk_label_new("TRAYPLAY");
    add_css_class(m_title_label, "trayplay-title");
    gtk_label_set_ellipsize(GTK_LABEL(m_title_label), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(m_title_label), 24);
    gtk_box_pack_start(GTK_BOX(header_box), m_title_label, FALSE, FALSE, 0);

    GtkWidget* spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(spacer, TRUE);
    gtk_box_pack_start(GTK_BOX(header_box), spacer, TRUE, TRUE, 0);

    m_status_badge = gtk_label_new("● WAITING");
    add_css_class(m_status_badge, "trayplay-status-badge");
    add_css_class(m_status_badge, "status-waiting");
    gtk_box_pack_start(GTK_BOX(header_box), m_status_badge, FALSE, FALSE, 0);

    GtkWidget* quit_btn = gtk_button_new_from_icon_name("system-shutdown-symbolic", GTK_ICON_SIZE_MENU);
    add_css_class(quit_btn, "trayplay-header-quit-btn");
    gtk_widget_set_tooltip_text(quit_btn, "Quit TrayPlay");
    gtk_widget_set_can_focus(quit_btn, FALSE);
    g_signal_connect(quit_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* self = static_cast<TrayPlayView*>(data);
        if (self->m_callbacks.on_quit_requested) {
            self->m_callbacks.on_quit_requested();
        }
    }), this);
    gtk_box_pack_end(GTK_BOX(header_box), quit_btn, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(m_root), header_box, FALSE, FALSE, 0);

    // 2. Main Stack ("game" <-> "browser")
    m_main_stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(m_main_stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_set_transition_duration(GTK_STACK(m_main_stack), 120);

    // Game View
    GtkWidget* game_view = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);

    GtkWidget* screen_frame = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    add_css_class(screen_frame, "trayplay-screen-frame");

    m_screen_stack = gtk_stack_new();
    add_css_class(m_screen_stack, "trayplay-screen");
    gtk_widget_set_size_request(m_screen_stack, 420, 236);
    gtk_stack_set_transition_type(GTK_STACK(m_screen_stack), GTK_STACK_TRANSITION_TYPE_NONE);

    // Placeholder page
    GtkWidget* placeholder_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_halign(placeholder_box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(placeholder_box, GTK_ALIGN_CENTER);

    m_placeholder_title = gtk_label_new("▶ TRAYPLAY ◀");
    add_css_class(m_placeholder_title, "trayplay-screen-title");
    gtk_box_pack_start(GTK_BOX(placeholder_box), m_placeholder_title, FALSE, FALSE, 0);

    m_placeholder_status = gtk_label_new("[ NO CARTRIDGE LOADED ]");
    add_css_class(m_placeholder_status, "trayplay-screen-status");
    gtk_label_set_line_wrap(GTK_LABEL(m_placeholder_status), TRUE);
    gtk_label_set_justify(GTK_LABEL(m_placeholder_status), GTK_JUSTIFY_CENTER);
    gtk_label_set_max_width_chars(GTK_LABEL(m_placeholder_status), 44);
    gtk_box_pack_start(GTK_BOX(placeholder_box), m_placeholder_status, FALSE, FALSE, 0);

    gtk_stack_add_named(GTK_STACK(m_screen_stack), placeholder_box, "placeholder");

    // Live Canvas page
    m_canvas = gtk_drawing_area_new();
    gtk_widget_set_size_request(m_canvas, 420, 236);
    gtk_widget_set_can_focus(m_canvas, TRUE);
    gtk_widget_add_events(m_canvas, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(m_canvas, "draw", G_CALLBACK(on_canvas_draw), this);
    g_signal_connect(m_canvas, "button-press-event", G_CALLBACK(+[](GtkWidget* w, GdkEventButton*, gpointer) -> gboolean {
        gtk_widget_grab_focus(w);
        return FALSE;
    }), nullptr);
    gtk_stack_add_named(GTK_STACK(m_screen_stack), m_canvas, "canvas");

    gtk_stack_set_visible_child_name(GTK_STACK(m_screen_stack), "placeholder");
    gtk_box_pack_start(GTK_BOX(screen_frame), m_screen_stack, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(game_view), screen_frame, FALSE, FALSE, 0);

    // Controls Row
    GtkWidget* controls_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget* load_btn = gtk_button_new_with_label("Load Game (.gpak)");
    add_css_class(load_btn, "trayplay-load-btn");
    add_css_class(load_btn, "suggested-action");
    gtk_widget_set_can_focus(load_btn, FALSE);
    gtk_widget_set_hexpand(load_btn, TRUE);
    g_signal_connect(load_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        static_cast<TrayPlayView*>(data)->open_browser();
    }), this);
    gtk_box_pack_start(GTK_BOX(controls_box), load_btn, TRUE, TRUE, 0);

    GtkWidget* eject_btn = gtk_button_new_with_label("Eject");
    add_css_class(eject_btn, "trayplay-action-btn");
    gtk_widget_set_can_focus(eject_btn, FALSE);
    g_signal_connect(eject_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        static_cast<TrayPlayView*>(data)->eject_cartridge();
    }), this);
    gtk_box_pack_end(GTK_BOX(controls_box), eject_btn, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(game_view), controls_box, FALSE, FALSE, 0);
    gtk_stack_add_named(GTK_STACK(m_main_stack), game_view, "game");

    // Cartridge Browser View
    CartridgeBrowser::Callbacks browser_cbs;
    browser_cbs.on_cartridge_selected = [this](const std::string& path) {
        load_cartridge(path);
    };
    browser_cbs.on_close_requested = [this]() {
        close_browser();
    };
    browser_cbs.on_modal_dialog_state = [this](bool active) {
        if (m_callbacks.on_modal_dialog_state) {
            m_callbacks.on_modal_dialog_state(active);
        }
    };
    m_browser = std::make_unique<CartridgeBrowser>(std::move(browser_cbs));
    gtk_stack_add_named(GTK_STACK(m_main_stack), m_browser->widget(), "browser");

    gtk_stack_set_visible_child_name(GTK_STACK(m_main_stack), "game");
    gtk_box_pack_start(GTK_BOX(m_root), m_main_stack, TRUE, TRUE, 0);

    gtk_widget_show_all(m_root);
}

TrayPlayView::~TrayPlayView() {
    *m_alive = false;
    stop_polling();
    release_all_buttons();
    m_ipc.close_shm();
    if (m_surface) {
        cairo_surface_destroy(m_surface);
        m_surface = nullptr;
    }
}

void TrayPlayView::set_parent_window(GtkWindow* parent_window) {
    m_parent_window = parent_window;
    if (m_browser) {
        m_browser->set_parent_window(parent_window);
    }
}

void TrayPlayView::on_popup_shown() {
    m_is_visible = true;
    m_ipc.ensure_daemon_async();
    poll_frame();
    start_polling();
    focus_canvas();
}

void TrayPlayView::on_popup_hidden() {
    m_is_visible = false;
    stop_polling();
    release_all_buttons();
    if (m_is_browser_active) {
        close_browser(false);
    }
}

void TrayPlayView::start_polling() {
    if (m_poll_timer_id != 0) return;

    m_poll_timer_id = g_timeout_add(16, +[](gpointer data) -> gboolean {
        auto* self = static_cast<TrayPlayView*>(data);
        if (!self->m_is_visible) {
            self->m_poll_timer_id = 0;
            return G_SOURCE_REMOVE;
        }
        self->poll_frame();
        return G_SOURCE_CONTINUE;
    }, this);
}

void TrayPlayView::stop_polling() {
    if (m_poll_timer_id != 0) {
        g_source_remove(m_poll_timer_id);
        m_poll_timer_id = 0;
    }
}

void TrayPlayView::poll_frame() {
    uint32_t* surface_pixels = nullptr;
    int surface_stride = 0;
    if (m_surface) {
        cairo_surface_flush(m_surface);
        surface_pixels = reinterpret_cast<uint32_t*>(cairo_image_surface_get_data(m_surface));
        surface_stride = cairo_image_surface_get_stride(m_surface);
    }

    const FrameSnapshot snap = m_ipc.poll_shm(surface_pixels, surface_stride, m_last_frame_index);
    if (!snap.shm_available) {
        if (!m_explicit_error) {
            set_waiting_state();
        }
        return;
    }

    const std::string display_title = snap.title.empty() ? "TRAYPLAY" : snap.title;

    if (snap.state == DaemonState::RUNNING) {
        m_explicit_error = false;
        if (snap.new_frame) {
            m_last_frame_index = snap.frame_index;
            m_stale_frame_ticks = 0;
            m_has_valid_frame = true;
            if (m_surface) {
                cairo_surface_mark_dirty(m_surface);
            }
            gtk_widget_queue_draw(m_canvas);
        } else {
            ++m_stale_frame_ticks;
            if (m_stale_frame_ticks > 90) {
                set_waiting_state();
                return;
            }
        }

        if (!m_is_browser_active) {
            if (m_has_valid_frame) {
                set_active_state(display_title, snap.fps);
            } else {
                set_waiting_state();
            }
        }
    } else if (snap.state == DaemonState::CRASHED) {
        if (!m_is_browser_active) {
            set_error_state();
        }
    } else if (snap.state == DaemonState::PAUSED) {
        if (!m_is_browser_active) {
            set_paused_state(display_title);
        }
    } else {
        if (!m_is_browser_active && !m_explicit_error) {
            set_waiting_state();
        }
    }
}

gboolean TrayPlayView::on_canvas_draw(GtkWidget* widget, cairo_t* cr, gpointer data) {
    auto* self = static_cast<TrayPlayView*>(data);
    const int alloc_w = gtk_widget_get_allocated_width(widget);
    const int alloc_h = gtk_widget_get_allocated_height(widget);

    if (self->m_surface && self->m_has_valid_frame) {
        cairo_save(cr);
        cairo_scale(
            cr,
            static_cast<double>(alloc_w) / static_cast<double>(IpcClient::VIRTUAL_WIDTH),
            static_cast<double>(alloc_h) / static_cast<double>(IpcClient::VIRTUAL_HEIGHT)
        );
        cairo_set_source_surface(cr, self->m_surface, 0.0, 0.0);
        cairo_pattern_t* pattern = cairo_get_source(cr);
        cairo_pattern_set_filter(pattern, CAIRO_FILTER_NEAREST);
        cairo_pattern_set_extend(pattern, CAIRO_EXTEND_PAD);
        cairo_paint(cr);
        cairo_restore(cr);
    } else {
        cairo_set_source_rgb(cr, 0.05, 0.06, 0.08);
        cairo_paint(cr);
    }
    return TRUE;
}

void TrayPlayView::open_browser() {
    m_is_browser_active = true;
    release_all_buttons();
    gtk_stack_set_visible_child_name(GTK_STACK(m_main_stack), "browser");
    set_title_text("LOAD CARTRIDGE");
    update_status_badge("● EXPLORER", "status-active");
    m_browser->open(m_parent_window);
}

void TrayPlayView::close_browser(bool refresh_game_view) {
    m_is_browser_active = false;
    gtk_stack_set_visible_child_name(GTK_STACK(m_main_stack), "game");
    m_last_frame_index = UINT64_MAX;
    if (refresh_game_view) {
        poll_frame();
        focus_canvas();
    }
}

void TrayPlayView::schedule_load_retry(const std::string& path, uint64_t request_id, int attempts_left) {
    struct RetryCtx {
        TrayPlayView* self;
        std::weak_ptr<bool> weak;
        std::string path;
        uint64_t request_id;
        int attempts_left;
    };
    auto* rctx = new RetryCtx{this, m_alive, path, request_id, attempts_left};
    g_timeout_add(40, +[](gpointer data) -> gboolean {
        auto* c = static_cast<RetryCtx*>(data);
        auto alive = c->weak.lock();
        if (!alive || !*alive || c->request_id != c->self->m_load_request_id) {
            delete c;
            return G_SOURCE_REMOVE;
        }

        if (!g_file_test(IpcClient::SOCKET_PATH, G_FILE_TEST_EXISTS)) {
            if (c->attempts_left > 1) {
                --c->attempts_left;
                return G_SOURCE_CONTINUE;
            }
            c->self->set_error_state("Daemon socket timeout");
            delete c;
            return G_SOURCE_REMOVE;
        }

        std::weak_ptr<bool> w = c->weak;
        const uint64_t req_id = c->request_id;
        const std::string cart_path = c->path;
        const int left = c->attempts_left;
        TrayPlayView* self = c->self;
        delete c;

        self->m_ipc.send_load_cartridge(cart_path, [self, w, req_id, cart_path, left](const IpcResponse& r2) {
            auto a2 = w.lock();
            if (!a2 || !*a2 || req_id != self->m_load_request_id) return;
            if (!r2.transport_ok) {
                if (left > 1) {
                    self->schedule_load_retry(cart_path, req_id, left - 1);
                } else {
                    self->set_error_state(r2.message.empty() ? "Failed to connect to daemon" : r2.message);
                }
                return;
            }
            if (!r2.ok) {
                self->set_error_state(r2.message.empty() ? "Failed to load cartridge" : r2.message);
            } else {
                self->focus_canvas();
            }
        });
        return G_SOURCE_REMOVE;
    }, rctx);
}

void TrayPlayView::load_cartridge(const std::string& path) {
    m_explicit_error = false;
    m_has_valid_frame = false;
    m_last_frame_index = UINT64_MAX;
    const uint64_t request_id = ++m_load_request_id;
    close_browser(true);

    std::weak_ptr<bool> weak = m_alive;
    m_ipc.send_load_cartridge(path, [this, weak, path, request_id](const IpcResponse& resp) {
        auto alive = weak.lock();
        if (!alive || !*alive || request_id != m_load_request_id) return;

        if (!resp.transport_ok) {
            m_ipc.ensure_daemon_async([this, weak, path, request_id](bool spawned) {
                auto alive2 = weak.lock();
                if (!alive2 || !*alive2 || request_id != m_load_request_id) return;
                if (!spawned) {
                    set_error_state("Failed to launch tray-console-daemon");
                    return;
                }
                schedule_load_retry(path, request_id, 25);
            });
            return;
        }

        if (!resp.ok) {
            set_error_state(resp.message);
        } else {
            focus_canvas();
        }
    });
}

void TrayPlayView::eject_cartridge() {
    ++m_load_request_id;
    m_explicit_error = false;
    release_all_buttons();
    set_waiting_state();

    std::weak_ptr<bool> weak = m_alive;
    m_ipc.send_unload([weak](const IpcResponse& resp) {
        auto alive = weak.lock();
        if (!alive || !*alive) return;
        if (!resp.transport_ok) {
            IpcClient::unlink_stale_shm();
        }
    });
}

void TrayPlayView::shutdown_daemon() {
    ++m_load_request_id;
    release_all_buttons();
    m_ipc.send_quit_and_cleanup();
}

void TrayPlayView::set_title_text(const std::string& text) {
    if (m_cached_title_text == text) return;
    m_cached_title_text = text;
    gtk_label_set_text(GTK_LABEL(m_title_label), text.c_str());
}

void TrayPlayView::set_screen_page(const char* page_name) {
    if (m_cached_screen_page == page_name) return;
    m_cached_screen_page = page_name;
    gtk_stack_set_visible_child_name(GTK_STACK(m_screen_stack), page_name);
}

void TrayPlayView::set_placeholder(const char* title, const std::string& status, bool is_error) {
    if (m_cached_placeholder_title != title) {
        m_cached_placeholder_title = title;
        gtk_label_set_text(GTK_LABEL(m_placeholder_title), title);
    }
    if (m_cached_placeholder_status != status) {
        m_cached_placeholder_status = status;
        gtk_label_set_text(GTK_LABEL(m_placeholder_status), status.c_str());
    }
    if (m_cached_placeholder_error != is_error) {
        m_cached_placeholder_error = is_error;
        if (is_error) {
            add_css_class(m_placeholder_status, "error-text");
        } else {
            remove_css_class(m_placeholder_status, "error-text");
        }
    }
}

void TrayPlayView::set_waiting_state() {
    m_current_state = DaemonState::STOPPED;
    m_has_valid_frame = false;
    m_last_frame_index = UINT64_MAX;
    m_stale_frame_ticks = 0;

    set_title_text("TRAYPLAY");
    set_screen_page("placeholder");
    set_placeholder("▶ TRAYPLAY ◀", "[ NO CARTRIDGE LOADED ]", false);
    update_status_badge("● WAITING", "status-waiting");
    notify_status(DaemonState::STOPPED, "TRAYPLAY");
}

void TrayPlayView::set_error_state(const std::string& msg) {
    m_current_state = DaemonState::CRASHED;
    m_explicit_error = true;
    m_has_valid_frame = false;
    m_last_frame_index = UINT64_MAX;
    m_stale_frame_ticks = 0;

    set_title_text("TRAYPLAY");
    set_screen_page("placeholder");
    const std::string upper_msg = msg.empty() ? "CARTRIDGE CRASHED" : to_upper_ascii(msg);
    set_placeholder("▶ ERROR ◀", "[ " + upper_msg + " ]", true);
    update_status_badge("● ERROR", "status-error");
    notify_status(DaemonState::CRASHED, upper_msg);
}

void TrayPlayView::set_paused_state(const std::string& title) {
    m_current_state = DaemonState::PAUSED;
    set_title_text(to_upper_ascii(title));
    update_status_badge("● PAUSED", "status-paused");
    notify_status(DaemonState::PAUSED, title);
}

void TrayPlayView::set_active_state(const std::string& title, float fps) {
    m_current_state = DaemonState::RUNNING;
    if (m_cached_screen_page != "canvas") {
        set_screen_page("canvas");
        focus_canvas();
    }

    set_title_text(to_upper_ascii(title));
    const int rounded_fps = static_cast<int>(std::lround(fps));
    if (rounded_fps != m_cached_fps || m_cached_badge_class != "status-active") {
        m_cached_fps = rounded_fps;
        char buf[48];
        std::snprintf(buf, sizeof(buf), "● ACTIVE (%d FPS)", rounded_fps);
        update_status_badge(buf, "status-active");
    }
    notify_status(DaemonState::RUNNING, title);
}

void TrayPlayView::update_status_badge(const char* text, const char* css_class) {
    if (std::strcmp(css_class, "status-active") != 0 || std::strncmp(text, "● ACTIVE", 8) != 0) {
        m_cached_fps = -1;
    }
    if (m_cached_badge_text != text) {
        m_cached_badge_text = text;
        gtk_label_set_text(GTK_LABEL(m_status_badge), text);
    }
    if (m_cached_badge_class != css_class) {
        if (!m_cached_badge_class.empty()) {
            remove_css_class(m_status_badge, m_cached_badge_class.c_str());
        }
        m_cached_badge_class = css_class;
        add_css_class(m_status_badge, css_class);
    }
}

void TrayPlayView::notify_status(DaemonState state, const std::string& title) {
    if (m_status_notified_once && state == m_last_notified_state && title == m_last_notified_title) {
        return;
    }
    m_status_notified_once = true;
    m_last_notified_state = state;
    m_last_notified_title = title;
    if (m_callbacks.on_status_changed) {
        m_callbacks.on_status_changed(state, title);
    }
}

void TrayPlayView::focus_canvas() {
    if (!m_is_browser_active && m_canvas) {
        gtk_widget_grab_focus(m_canvas);
    }
}

void TrayPlayView::release_all_buttons() {
    if (m_pressed_buttons.empty()) return;
    for (const auto& btn : m_pressed_buttons) {
        m_ipc.send_button(btn, false);
    }
    m_pressed_buttons.clear();
}

const char* TrayPlayView::map_key_to_button(guint keyval) {
    switch (keyval) {
        case GDK_KEY_Up:
        case GDK_KEY_w:
        case GDK_KEY_W:
        case GDK_KEY_Cyrillic_tse:
        case GDK_KEY_Cyrillic_TSE:
            return "up";
        case GDK_KEY_Down:
        case GDK_KEY_s:
        case GDK_KEY_S:
        case GDK_KEY_Cyrillic_yeru:
        case GDK_KEY_Cyrillic_YERU:
            return "down";
        case GDK_KEY_Left:
        case GDK_KEY_a:
        case GDK_KEY_A:
        case GDK_KEY_Cyrillic_ef:
        case GDK_KEY_Cyrillic_EF:
            return "left";
        case GDK_KEY_Right:
        case GDK_KEY_d:
        case GDK_KEY_D:
        case GDK_KEY_Cyrillic_ve:
        case GDK_KEY_Cyrillic_VE:
            return "right";
        case GDK_KEY_space:
            return "a";
        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter:
            return "start";
        case GDK_KEY_Escape:
            return "select";
        case GDK_KEY_m:
        case GDK_KEY_M:
        case GDK_KEY_Cyrillic_softsign:
        case GDK_KEY_Cyrillic_SOFTSIGN:
            return "m";
        default:
            return nullptr;
    }
}

bool TrayPlayView::handle_key_press(const GdkEventKey* event) {
    if (m_is_browser_active) {
        if (event->keyval == GDK_KEY_Escape) {
            close_browser();
            return true;
        }
        return false;
    }

    if (event->keyval == GDK_KEY_Escape && m_current_state != DaemonState::RUNNING) {
        if (m_callbacks.on_hide_requested) {
            m_callbacks.on_hide_requested();
        }
        return true;
    }

    const char* btn = map_key_to_button(event->keyval);
    if (!btn) {
        return false;
    }

    // Deduplicate X11 key-repeat events so holding movement keys stays smooth
    if (m_pressed_buttons.insert(btn).second) {
        m_ipc.send_button(btn, true);
    }
    return true;
}

bool TrayPlayView::handle_key_release(const GdkEventKey* event) {
    if (m_is_browser_active) {
        return false;
    }

    const char* btn = map_key_to_button(event->keyval);
    if (!btn) {
        return false;
    }

    if (m_pressed_buttons.erase(btn) > 0) {
        m_ipc.send_button(btn, false);
    }
    return true;
}

} // namespace trayplay::xfce
