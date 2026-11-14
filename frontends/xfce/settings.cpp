#include "settings.h"

#include <glib.h>
#include <nlohmann/json.hpp>

#include <fstream>

namespace trayplay::xfce {

namespace {

std::string get_config_dir() {
    const char* user_config = g_get_user_config_dir();
    return std::string(user_config ? user_config : "") + "/trayplay";
}

std::string get_config_path() {
    return get_config_dir() + "/settings.json";
}

} // namespace

Settings load_settings() {
    Settings settings;
    const std::string path = get_config_path();
    std::ifstream in(path);
    if (!in.is_open()) {
        return settings;
    }

    try {
        nlohmann::json j;
        in >> j;
        if (j.is_object() && j.contains("last_directory") && j["last_directory"].is_string()) {
            settings.last_directory = j["last_directory"].get<std::string>();
        }
    } catch (...) {
        // Fallback to default settings if file is malformed
    }
    return settings;
}

void save_last_directory(const std::string& dir_path) {
    const std::string dir = get_config_dir();
    const std::string path = get_config_path();

    nlohmann::json j = nlohmann::json::object();
    {
        std::ifstream in(path);
        if (in.is_open()) {
            try {
                in >> j;
                if (!j.is_object()) {
                    j = nlohmann::json::object();
                }
            } catch (...) {
                j = nlohmann::json::object();
            }
        }
    }

    j["last_directory"] = dir_path;

    g_mkdir_with_parents(dir.c_str(), 0755);
    std::ofstream out(path);
    if (out.is_open()) {
        out << j.dump(2) << std::endl;
    }
}

} // namespace trayplay::xfce
