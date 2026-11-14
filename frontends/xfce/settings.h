#pragma once

#include <string>

namespace trayplay::xfce {

struct Settings {
    std::string last_directory;
};

Settings load_settings();
void save_last_directory(const std::string& dir_path);

} // namespace trayplay::xfce
