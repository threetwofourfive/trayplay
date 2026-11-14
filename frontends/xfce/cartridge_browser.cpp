#include "cartridge_browser.h"
#include "settings.h"

#include <gio/gio.h>
#include <glib.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <unistd.h>
#include <vector>

namespace trayplay::xfce {

namespace {

struct BrowserEntry {
    std::string name;
    std::string sort_key;
    std::string full_path;
    bool is_dir = false;
    bool has_game_json = false;
    bool is_gpak = false;
    bool is_lua = false;
    bool is_game_json = false;
    uint64_t size = 0;
};

struct DirectoryScanResult {
    std::string dir_path;
    uint64_t generation = 0;
    bool current_has_game_json = false;
    std::vector<BrowserEntry> items;
};

struct DirectoryScanTaskData {
    std::string dir_path;
    uint64_t generation = 0;
};

struct ScanUiCallbackCtx {
    CartridgeBrowser* self = nullptr;
    std::weak_ptr<bool> weak;
    std::function<void(const DirectoryScanResult&)> on_done;
};

std::string to_lower_ascii(const std::string& s) {
    std::string out = s;
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

bool ends_with(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string format_file_size(uint64_t bytes) {
    if (bytes == 0) return "0 B";
    char buf[32];
    if (bytes < 1024) {
        std::snprintf(buf, sizeof(buf), "%llu B", static_cast<unsigned long long>(bytes));
    } else if (bytes < 1024ULL * 1024ULL) {
        std::snprintf(buf, sizeof(buf), "%.1f KB", static_cast<double>(bytes) / 1024.0);
    } else {
        std::snprintf(buf, sizeof(buf), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    }
    return buf;
}

std::string executable_directory() {
    char buf[4096];
    ssize_t len = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len <= 0) return {};
    buf[len] = '\0';
    gchar* dir = g_path_get_dirname(buf);
    std::string res = dir ? dir : "";
    g_free(dir);
    return res;
}

void add_css_class(GtkWidget* widget, const char* cls) {
    gtk_style_context_add_class(gtk_widget_get_style_context(widget), cls);
}

std::vector<BrowserEntry> list_directory_entries(const std::string& dir_path, GCancellable* cancellable) {
    std::vector<BrowserEntry> items;
    GFile* dir = g_file_new_for_path(dir_path.c_str());
    GError* err = nullptr;
    GFileEnumerator* enumerator = g_file_enumerate_children(
        dir,
        "standard::name,standard::type,standard::size",
        G_FILE_QUERY_INFO_NONE,
        cancellable,
        &err
    );
    if (err) {
        g_error_free(err);
    }

    if (enumerator) {
        while (!g_cancellable_is_cancelled(cancellable)) {
            GFileInfo* info = g_file_enumerator_next_file(enumerator, cancellable, nullptr);
            if (!info) break;

            const char* raw_name = g_file_info_get_name(info);
            if (!raw_name || raw_name[0] == '.') {
                g_object_unref(info);
                continue;
            }

            std::string name(raw_name);
            std::string lower = to_lower_ascii(name);
            const bool is_dir = (g_file_info_get_file_type(info) == G_FILE_TYPE_DIRECTORY);

            gchar* child_path_c = g_build_filename(dir_path.c_str(), name.c_str(), nullptr);
            std::string full_path = child_path_c ? child_path_c : "";
            g_free(child_path_c);

            if (is_dir) {
                gchar* json_path = g_build_filename(full_path.c_str(), "game.json", nullptr);
                bool has_json = json_path && g_file_test(json_path, G_FILE_TEST_EXISTS);
                g_free(json_path);

                gchar* ckey = g_utf8_collate_key_for_filename(name.c_str(), -1);
                BrowserEntry e;
                e.sort_key = ckey ? ckey : lower;
                g_free(ckey);
                e.name = std::move(name);
                e.full_path = std::move(full_path);
                e.is_dir = true;
                e.has_game_json = has_json;
                items.push_back(std::move(e));
            } else if (ends_with(lower, ".gpak") || ends_with(lower, ".lua") || lower == "game.json") {
                gchar* ckey = g_utf8_collate_key_for_filename(name.c_str(), -1);
                BrowserEntry e;
                e.sort_key = ckey ? ckey : lower;
                g_free(ckey);
                e.name = std::move(name);
                e.full_path = std::move(full_path);
                e.is_dir = false;
                e.size = static_cast<uint64_t>(std::max<goffset>(0, g_file_info_get_size(info)));
                e.is_gpak = ends_with(lower, ".gpak");
                e.is_lua = ends_with(lower, ".lua");
                e.is_game_json = (lower == "game.json");
                items.push_back(std::move(e));
            }

            g_object_unref(info);
        }
        g_file_enumerator_close(enumerator, nullptr, nullptr);
        g_object_unref(enumerator);
    }

    g_object_unref(dir);

    if (g_cancellable_is_cancelled(cancellable)) {
        return {};
    }

    std::sort(items.begin(), items.end(), [](const BrowserEntry& a, const BrowserEntry& b) {
        if (a.is_dir != b.is_dir) {
            return a.is_dir > b.is_dir;
        }
        return a.sort_key < b.sort_key;
    });

    return items;
}

} // namespace

CartridgeBrowser::CartridgeBrowser(Callbacks callbacks)
    : m_callbacks(std::move(callbacks)) {
    m_root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    add_css_class(m_root, "trayplay-browser-view");
    gtk_widget_set_size_request(m_root, 428, 284);
    gtk_widget_set_hexpand(m_root, TRUE);
    gtk_widget_set_vexpand(m_root, FALSE);

    // 1. Top Navigation Bar
    GtkWidget* nav_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    add_css_class(nav_bar, "trayplay-browser-top-bar");

    m_up_btn = gtk_button_new_from_icon_name("go-up-symbolic", GTK_ICON_SIZE_MENU);
    add_css_class(m_up_btn, "trayplay-browser-icon-btn");
    gtk_widget_set_tooltip_text(m_up_btn, "Parent Directory");
    g_signal_connect(m_up_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        static_cast<CartridgeBrowser*>(data)->navigate_up();
    }), this);
    gtk_box_pack_start(GTK_BOX(nav_bar), m_up_btn, FALSE, FALSE, 0);

    m_path_label = gtk_label_new("");
    add_css_class(m_path_label, "trayplay-browser-path-label");
    gtk_label_set_ellipsize(GTK_LABEL(m_path_label), PANGO_ELLIPSIZE_START);
    gtk_label_set_xalign(GTK_LABEL(m_path_label), 0.0f);
    gtk_widget_set_hexpand(m_path_label, TRUE);
    gtk_box_pack_start(GTK_BOX(nav_bar), m_path_label, TRUE, TRUE, 0);

    GtkWidget* close_btn = gtk_button_new_from_icon_name("window-close-symbolic", GTK_ICON_SIZE_MENU);
    add_css_class(close_btn, "trayplay-browser-icon-btn");
    add_css_class(close_btn, "trayplay-browser-close-btn");
    gtk_widget_set_tooltip_text(close_btn, "Close Explorer");
    g_signal_connect(close_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* self = static_cast<CartridgeBrowser*>(data);
        if (self->m_callbacks.on_close_requested) {
            self->m_callbacks.on_close_requested();
        }
    }), this);
    gtk_box_pack_end(GTK_BOX(nav_bar), close_btn, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(m_root), nav_bar, FALSE, FALSE, 0);

    // 2. Shortcuts Bar (Quick Jump)
    GtkWidget* shortcuts_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    add_css_class(shortcuts_bar, "trayplay-browser-shortcuts-bar");

    GtkWidget* jump_label = gtk_label_new("JUMP:");
    add_css_class(jump_label, "trayplay-browser-jump-label");
    gtk_box_pack_start(GTK_BOX(shortcuts_bar), jump_label, FALSE, FALSE, 0);

    GtkWidget* games_btn = gtk_button_new_with_label("Games");
    add_css_class(games_btn, "trayplay-browser-shortcut-btn");
    g_signal_connect(games_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* self = static_cast<CartridgeBrowser*>(data);
        self->navigate_to(resolve_default_games_dir());
    }), this);
    gtk_box_pack_start(GTK_BOX(shortcuts_bar), games_btn, FALSE, FALSE, 0);

    GtkWidget* home_btn = gtk_button_new_with_label("Home");
    add_css_class(home_btn, "trayplay-browser-shortcut-btn");
    g_signal_connect(home_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* self = static_cast<CartridgeBrowser*>(data);
        const char* home = g_get_home_dir();
        self->navigate_to(home ? home : "/");
    }), this);
    gtk_box_pack_start(GTK_BOX(shortcuts_bar), home_btn, FALSE, FALSE, 0);

    const char* downloads_dir = g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD);
    if (downloads_dir && g_file_test(downloads_dir, G_FILE_TEST_IS_DIR)) {
        GtkWidget* dl_btn = gtk_button_new_with_label("Downloads");
        add_css_class(dl_btn, "trayplay-browser-shortcut-btn");
        g_signal_connect(dl_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
            auto* self = static_cast<CartridgeBrowser*>(data);
            const char* dl = g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD);
            if (dl) self->navigate_to(dl);
        }), this);
        gtk_box_pack_start(GTK_BOX(shortcuts_bar), dl_btn, FALSE, FALSE, 0);
    }

    GtkWidget* sys_btn = gtk_button_new_with_label("System...");
    add_css_class(sys_btn, "trayplay-browser-system-btn");
    g_signal_connect(sys_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        static_cast<CartridgeBrowser*>(data)->open_system_file_chooser();
    }), this);
    gtk_box_pack_end(GTK_BOX(shortcuts_bar), sys_btn, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(m_root), shortcuts_bar, FALSE, FALSE, 0);

    // 3. Scrollable File List Area
    m_list_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    add_css_class(m_list_box, "trayplay-browser-list");
    gtk_widget_set_hexpand(m_list_box, TRUE);

    m_scroll_window = gtk_scrolled_window_new(nullptr, nullptr);
    add_css_class(m_scroll_window, "trayplay-browser-scroll");
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(m_scroll_window), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(m_scroll_window), GTK_SHADOW_NONE);
    gtk_widget_set_hexpand(m_scroll_window, TRUE);
    gtk_widget_set_vexpand(m_scroll_window, TRUE);
    gtk_container_add(GTK_CONTAINER(m_scroll_window), m_list_box);

    gtk_box_pack_start(GTK_BOX(m_root), m_scroll_window, TRUE, TRUE, 0);

    // 4. Bottom Action Bar
    GtkWidget* bottom_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    add_css_class(bottom_bar, "trayplay-browser-bottom-bar");

    GtkWidget* hint_label = gtk_label_new("Select .gpak or .lua cartridge • Esc to return");
    add_css_class(hint_label, "trayplay-browser-hint");
    gtk_label_set_xalign(GTK_LABEL(hint_label), 0.0f);
    gtk_widget_set_hexpand(hint_label, TRUE);
    gtk_box_pack_start(GTK_BOX(bottom_bar), hint_label, TRUE, TRUE, 0);

    GtkWidget* back_btn = gtk_button_new_with_label("Back");
    add_css_class(back_btn, "trayplay-browser-cancel-btn");
    g_signal_connect(back_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* self = static_cast<CartridgeBrowser*>(data);
        if (self->m_callbacks.on_close_requested) {
            self->m_callbacks.on_close_requested();
        }
    }), this);
    gtk_box_pack_end(GTK_BOX(bottom_bar), back_btn, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(m_root), bottom_bar, FALSE, FALSE, 0);
}

CartridgeBrowser::~CartridgeBrowser() {
    *m_alive = false;
    if (m_scan_cancellable) {
        g_cancellable_cancel(m_scan_cancellable);
        g_clear_object(&m_scan_cancellable);
    }
}

std::string CartridgeBrowser::resolve_default_games_dir() {
    std::vector<std::string> candidates;

    if (const char* env_dir = g_getenv("TRAYPLAY_GAMES_DIR")) {
        candidates.emplace_back(env_dir);
    }

    const std::string exe_dir = executable_directory();
    if (!exe_dir.empty()) {
        candidates.push_back(exe_dir + "/games");
        candidates.push_back(exe_dir + "/../games");
        candidates.push_back(exe_dir + "/../share/trayplay/games");
    }

    candidates.emplace_back("/usr/share/trayplay/games");
    candidates.emplace_back("/usr/local/share/trayplay/games");

    if (const char* data_dir = g_get_user_data_dir()) {
        candidates.push_back(std::string(data_dir) + "/trayplay/games");
    }
    if (const char* home_dir = g_get_home_dir()) {
        candidates.push_back(std::string(home_dir) + "/Games");
    }

    for (const auto& dir : candidates) {
        if (!dir.empty() && g_file_test(dir.c_str(), G_FILE_TEST_IS_DIR)) {
            gchar* real = g_canonicalize_filename(dir.c_str(), nullptr);
            std::string canonical = real ? real : dir;
            g_free(real);
            return canonical;
        }
    }

    const char* home = g_get_home_dir();
    return home ? home : "/";
}

void CartridgeBrowser::open(GtkWindow* parent_window) {
    if (parent_window) {
        m_parent_window = parent_window;
    }

    std::string target_dir = m_current_dir;
    if (target_dir.empty() || !g_file_test(target_dir.c_str(), G_FILE_TEST_IS_DIR)) {
        const Settings settings = load_settings();
        if (!settings.last_directory.empty() &&
            g_file_test(settings.last_directory.c_str(), G_FILE_TEST_IS_DIR)) {
            target_dir = settings.last_directory;
        } else {
            target_dir = resolve_default_games_dir();
        }
    }

    navigate_to(target_dir);
}

void CartridgeBrowser::navigate_to(const std::string& dir_path) {
    std::string target = dir_path;
    if (target.empty() || !g_file_test(target.c_str(), G_FILE_TEST_IS_DIR)) {
        target = resolve_default_games_dir();
    }
    m_current_dir = target;
    if (m_last_saved_dir != target) {
        m_last_saved_dir = target;
        save_last_directory(target);
    }
    start_directory_scan(target);
}

void CartridgeBrowser::navigate_up() {
    if (m_current_dir.empty() || m_current_dir == "/") {
        return;
    }
    GFile* file = g_file_new_for_path(m_current_dir.c_str());
    GFile* parent = g_file_get_parent(file);
    if (parent) {
        gchar* p = g_file_get_path(parent);
        if (p) {
            navigate_to(p);
            g_free(p);
        }
        g_object_unref(parent);
    }
    g_object_unref(file);
}

void CartridgeBrowser::clear_list() {
    GList* children = gtk_container_get_children(GTK_CONTAINER(m_list_box));
    for (GList* l = children; l != nullptr; l = l->next) {
        gtk_widget_destroy(GTK_WIDGET(l->data));
    }
    g_list_free(children);
}

void CartridgeBrowser::start_directory_scan(const std::string& dir_path) {
    if (m_scan_cancellable) {
        g_cancellable_cancel(m_scan_cancellable);
        g_clear_object(&m_scan_cancellable);
    }

    const bool is_root = (dir_path.empty() || dir_path == "/");
    gtk_widget_set_sensitive(m_up_btn, !is_root);
    gtk_label_set_text(GTK_LABEL(m_path_label), dir_path.c_str());

    const uint64_t gen = ++m_scan_generation;
    m_scan_cancellable = g_cancellable_new();

    auto* ui_ctx = new ScanUiCallbackCtx();
    ui_ctx->self = this;
    ui_ctx->weak = m_alive;
    ui_ctx->on_done = [this](const DirectoryScanResult& scan) {
        clear_list();

        if (scan.current_has_game_json) {
            gchar* base = g_path_get_basename(scan.dir_path.c_str());
            std::string folder_name = base ? base : "Current Directory";
            g_free(base);

            GtkWidget* btn = gtk_button_new();
            add_css_class(btn, "trayplay-browser-item");
            add_css_class(btn, "trayplay-browser-item-cart");

            GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
            GtkWidget* icon = gtk_image_new_from_icon_name("media-playback-start-symbolic", GTK_ICON_SIZE_MENU);
            add_css_class(icon, "trayplay-icon-cart");
            gtk_box_pack_start(GTK_BOX(row), icon, FALSE, FALSE, 0);

            std::string play_text = "Run project (" + folder_name + ")";
            GtkWidget* label = gtk_label_new(play_text.c_str());
            add_css_class(label, "trayplay-browser-item-cart-name");
            gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
            gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
            gtk_widget_set_hexpand(label, TRUE);
            gtk_box_pack_start(GTK_BOX(row), label, TRUE, TRUE, 0);

            GtkWidget* badge = gtk_label_new("[PLAY PROJECT]");
            add_css_class(badge, "trayplay-browser-cart-badge");
            gtk_box_pack_end(GTK_BOX(row), badge, FALSE, FALSE, 0);

            gtk_container_add(GTK_CONTAINER(btn), row);

            auto* path_copy = new std::string(scan.dir_path);
            g_object_set_data_full(G_OBJECT(btn), "trayplay-path", path_copy, +[](gpointer p) {
                delete static_cast<std::string*>(p);
            });
            g_signal_connect(btn, "clicked", G_CALLBACK(+[](GtkButton* b, gpointer data) {
                auto* self = static_cast<CartridgeBrowser*>(data);
                auto* p = static_cast<std::string*>(g_object_get_data(G_OBJECT(b), "trayplay-path"));
                if (p && self->m_callbacks.on_cartridge_selected) {
                    self->m_callbacks.on_cartridge_selected(*p);
                }
            }), this);

            gtk_box_pack_start(GTK_BOX(m_list_box), btn, FALSE, FALSE, 0);
        }

        if (scan.items.empty() && !scan.current_has_game_json) {
            GtkWidget* empty_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
            add_css_class(empty_box, "trayplay-browser-empty");
            gtk_widget_set_halign(empty_box, GTK_ALIGN_CENTER);
            gtk_widget_set_valign(empty_box, GTK_ALIGN_CENTER);
            gtk_widget_set_vexpand(empty_box, TRUE);

            GtkWidget* empty_icon = gtk_image_new_from_icon_name("folder-open-symbolic", GTK_ICON_SIZE_DIALOG);
            add_css_class(empty_icon, "trayplay-browser-empty-icon");
            gtk_box_pack_start(GTK_BOX(empty_box), empty_icon, FALSE, FALSE, 0);

            GtkWidget* empty_label = gtk_label_new("No cartridges (.gpak, .lua) or subfolders here");
            add_css_class(empty_label, "trayplay-browser-empty-label");
            gtk_box_pack_start(GTK_BOX(empty_box), empty_label, FALSE, FALSE, 0);

            gtk_box_pack_start(GTK_BOX(m_list_box), empty_box, TRUE, TRUE, 0);
            gtk_widget_show_all(m_list_box);
            return;
        }

        for (const auto& item : scan.items) {
            GtkWidget* btn = gtk_button_new();
            add_css_class(btn, "trayplay-browser-item");
            add_css_class(btn, item.is_dir ? "trayplay-browser-item-dir" : "trayplay-browser-item-cart");

            GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);

            const char* icon_name = "media-optical-symbolic";
            const char* icon_class = "trayplay-icon-cart";
            if (item.is_dir) {
                icon_name = "folder-symbolic";
                icon_class = "trayplay-icon-dir";
            } else if (item.is_lua) {
                icon_name = "text-x-script-symbolic";
                icon_class = "trayplay-icon-lua";
            } else if (item.is_gpak) {
                icon_name = "applications-games-symbolic";
                icon_class = "trayplay-icon-cart";
            }

            GtkWidget* icon = gtk_image_new_from_icon_name(icon_name, GTK_ICON_SIZE_MENU);
            add_css_class(icon, icon_class);
            gtk_box_pack_start(GTK_BOX(row), icon, FALSE, FALSE, 0);

            std::string display_name = item.is_dir ? (item.name + "/") : item.name;
            GtkWidget* name_label = gtk_label_new(display_name.c_str());
            add_css_class(name_label, item.is_dir ? "trayplay-browser-item-dir-name" : "trayplay-browser-item-cart-name");
            gtk_label_set_ellipsize(GTK_LABEL(name_label), PANGO_ELLIPSIZE_END);
            gtk_label_set_xalign(GTK_LABEL(name_label), 0.0f);
            gtk_widget_set_hexpand(name_label, TRUE);
            gtk_box_pack_start(GTK_BOX(row), name_label, TRUE, TRUE, 0);

            if (item.is_dir) {
                if (item.has_game_json) {
                    GtkWidget* pbadge = gtk_label_new("[PROJECT]");
                    add_css_class(pbadge, "trayplay-browser-project-badge");
                    gtk_box_pack_start(GTK_BOX(row), pbadge, FALSE, FALSE, 0);
                }
                GtkWidget* arrow = gtk_label_new("›");
                add_css_class(arrow, "trayplay-browser-arrow");
                gtk_box_pack_end(GTK_BOX(row), arrow, FALSE, FALSE, 0);
            } else {
                GtkWidget* size_label = gtk_label_new(format_file_size(item.size).c_str());
                add_css_class(size_label, "trayplay-browser-item-size");
                gtk_box_pack_start(GTK_BOX(row), size_label, FALSE, FALSE, 0);

                const char* badge_text = "[CART]";
                const char* badge_class = "trayplay-browser-cart-badge";
                if (item.is_lua) {
                    badge_text = "[LUA]";
                    badge_class = "trayplay-browser-lua-badge";
                } else if (item.is_game_json) {
                    badge_text = "[JSON]";
                    badge_class = "trayplay-browser-lua-badge";
                }
                GtkWidget* badge = gtk_label_new(badge_text);
                add_css_class(badge, badge_class);
                gtk_box_pack_end(GTK_BOX(row), badge, FALSE, FALSE, 0);
            }

            gtk_container_add(GTK_CONTAINER(btn), row);

            auto* path_copy = new std::string(item.full_path);
            g_object_set_data_full(G_OBJECT(btn), "trayplay-path", path_copy, +[](gpointer p) {
                delete static_cast<std::string*>(p);
            });
            g_object_set_data(G_OBJECT(btn), "trayplay-is-dir", GINT_TO_POINTER(item.is_dir ? 1 : 0));

            g_signal_connect(btn, "clicked", G_CALLBACK(+[](GtkButton* b, gpointer data) {
                auto* self = static_cast<CartridgeBrowser*>(data);
                auto* p = static_cast<std::string*>(g_object_get_data(G_OBJECT(b), "trayplay-path"));
                const bool is_dir = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "trayplay-is-dir")) != 0;
                if (!p) return;
                if (is_dir) {
                    self->navigate_to(*p);
                } else if (self->m_callbacks.on_cartridge_selected) {
                    self->m_callbacks.on_cartridge_selected(*p);
                }
            }), this);

            gtk_box_pack_start(GTK_BOX(m_list_box), btn, FALSE, FALSE, 0);
        }

        gtk_widget_show_all(m_list_box);

        if (GtkAdjustment* vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(m_scroll_window))) {
            gtk_adjustment_set_value(vadj, gtk_adjustment_get_lower(vadj));
        }
    };

    GTask* task = g_task_new(
        nullptr,
        m_scan_cancellable,
        +[](GObject*, GAsyncResult* res, gpointer data) {
            auto* cb = static_cast<ScanUiCallbackCtx*>(data);
            GError* err = nullptr;
            auto* scan_res = static_cast<DirectoryScanResult*>(g_task_propagate_pointer(G_TASK(res), &err));
            if (err) {
                g_error_free(err);
            }

            if (auto alive = cb->weak.lock(); alive && *alive && scan_res) {
                if (scan_res->generation == cb->self->m_scan_generation) {
                    cb->on_done(*scan_res);
                }
            }
            delete scan_res;
            delete cb;
        },
        ui_ctx
    );

    auto* tdata = new DirectoryScanTaskData{dir_path, gen};
    g_task_set_task_data(task, tdata, +[](gpointer p) {
        delete static_cast<DirectoryScanTaskData*>(p);
    });

    g_task_run_in_thread(task, +[](GTask* t, gpointer, gpointer task_data, GCancellable* cancellable) {
        auto* td = static_cast<DirectoryScanTaskData*>(task_data);
        auto* res = new DirectoryScanResult();
        res->dir_path = td->dir_path;
        res->generation = td->generation;

        if (!g_cancellable_is_cancelled(cancellable)) {
            gchar* current_json = g_build_filename(td->dir_path.c_str(), "game.json", nullptr);
            res->current_has_game_json = current_json && g_file_test(current_json, G_FILE_TEST_EXISTS);
            g_free(current_json);
            res->items = list_directory_entries(td->dir_path, cancellable);
        }

        if (g_cancellable_is_cancelled(cancellable)) {
            delete res;
            g_task_return_new_error(t, G_IO_ERROR, G_IO_ERROR_CANCELLED, "Directory scan cancelled");
            return;
        }
        g_task_return_pointer(t, res, +[](gpointer p) {
            delete static_cast<DirectoryScanResult*>(p);
        });
    });

    g_object_unref(task);
}

void CartridgeBrowser::open_system_file_chooser() {
    if (m_callbacks.on_modal_dialog_state) {
        m_callbacks.on_modal_dialog_state(true);
    }

    GtkFileChooserNative* chooser = gtk_file_chooser_native_new(
        "Select TrayPlay Cartridge (.gpak, .lua, game.json)",
        m_parent_window,
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Open",
        "_Cancel"
    );

    if (!m_current_dir.empty() && g_file_test(m_current_dir.c_str(), G_FILE_TEST_IS_DIR)) {
        gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(chooser), m_current_dir.c_str());
    }

    GtkFileFilter* cart_filter = gtk_file_filter_new();
    gtk_file_filter_set_name(cart_filter, "Game Cartridge (*.gpak, *.lua, game.json)");
    gtk_file_filter_add_pattern(cart_filter, "*.gpak");
    gtk_file_filter_add_pattern(cart_filter, "*.lua");
    gtk_file_filter_add_pattern(cart_filter, "game.json");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(chooser), cart_filter);

    GtkFileFilter* all_filter = gtk_file_filter_new();
    gtk_file_filter_set_name(all_filter, "All Files");
    gtk_file_filter_add_pattern(all_filter, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(chooser), all_filter);

    struct ChooserCtx {
        CartridgeBrowser* self;
        std::weak_ptr<bool> weak;
    };
    auto* ctx = new ChooserCtx{this, m_alive};

    g_signal_connect(chooser, "response", G_CALLBACK(+[](GtkNativeDialog* dlg, gint response_id, gpointer data) {
        auto* c = static_cast<ChooserCtx*>(data);
        std::string selected_path;
        if (response_id == GTK_RESPONSE_ACCEPT) {
            gchar* fn = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
            if (fn) {
                selected_path = fn;
                g_free(fn);
            }
        }

        gtk_native_dialog_destroy(dlg);
        g_object_unref(dlg);

        if (auto alive = c->weak.lock(); alive && *alive) {
            if (c->self->m_callbacks.on_modal_dialog_state) {
                c->self->m_callbacks.on_modal_dialog_state(false);
            }
            if (!selected_path.empty()) {
                gchar* parent = g_path_get_dirname(selected_path.c_str());
                if (parent) {
                    save_last_directory(parent);
                    c->self->m_current_dir = parent;
                    g_free(parent);
                }
                if (c->self->m_callbacks.on_cartridge_selected) {
                    c->self->m_callbacks.on_cartridge_selected(selected_path);
                }
            }
        }
        delete c;
    }), ctx);

    gtk_native_dialog_show(GTK_NATIVE_DIALOG(chooser));
}

} // namespace trayplay::xfce
