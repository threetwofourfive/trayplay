#pragma once

#include <gio/gio.h>
#include <gtk/gtk.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace trayplay::xfce {

class CartridgeBrowser {
public:
    struct Callbacks {
        std::function<void(const std::string& path)> on_cartridge_selected;
        std::function<void()> on_close_requested;
        std::function<void(bool active)> on_modal_dialog_state;
    };

    explicit CartridgeBrowser(Callbacks callbacks);
    ~CartridgeBrowser();

    CartridgeBrowser(const CartridgeBrowser&) = delete;
    CartridgeBrowser& operator=(const CartridgeBrowser&) = delete;

    GtkWidget* widget() const { return m_root; }

    void open(GtkWindow* parent_window = nullptr);
    void set_parent_window(GtkWindow* parent_window) { m_parent_window = parent_window; }

    static std::string resolve_default_games_dir();

private:
    void navigate_to(const std::string& dir_path);
    void navigate_up();
    void start_directory_scan(const std::string& dir_path);
    void open_system_file_chooser();
    void clear_list();

    Callbacks m_callbacks;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);

    GtkWindow* m_parent_window = nullptr;
    GtkWidget* m_root = nullptr;
    GtkWidget* m_up_btn = nullptr;
    GtkWidget* m_path_label = nullptr;
    GtkWidget* m_list_box = nullptr;
    GtkWidget* m_scroll_window = nullptr;

    GCancellable* m_scan_cancellable = nullptr;
    uint64_t m_scan_generation = 0;

    std::string m_current_dir;
    std::string m_last_saved_dir;
};

} // namespace trayplay::xfce
