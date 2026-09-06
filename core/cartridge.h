#pragma once

#include <string>
#include <optional>
#include <vector>
#include <cstdint>

namespace trayplay {

struct CartridgeMetadata {
    std::string title = "Untitled Game";
    std::string author = "Unknown";
    std::string version = "1.0.0";
    std::string description = "";
    std::string entry_point = "main.lua";
};

class Cartridge {
public:
    Cartridge() = default;

    // Load a cartridge from .gpak (ZIP archive), .lua (raw script), or directory
    static std::optional<Cartridge> load(const std::string& path, std::string& out_error);

    const CartridgeMetadata& get_metadata() const noexcept { return m_metadata; }
    const std::string& get_code() const noexcept { return m_code; }
    const std::string& get_file_path() const noexcept { return m_path; }

    bool is_valid() const noexcept { return !m_code.empty(); }

private:
    static std::optional<Cartridge> load_lua(const std::string& path, std::string& out_error);
    static std::optional<Cartridge> load_gpak(const std::string& path, std::string& out_error);
    static std::optional<Cartridge> load_dir(const std::string& path, std::string& out_error);

    CartridgeMetadata m_metadata;
    std::string m_code;
    std::string m_path;
};

} // namespace trayplay
