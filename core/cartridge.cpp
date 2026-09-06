#include "cartridge.h"
#include <miniz/miniz.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstring>
#include <iostream>

namespace trayplay {

namespace fs = std::filesystem;

std::optional<Cartridge> Cartridge::load(const std::string& path, std::string& out_error) {
    fs::path target_path = path;

    // Smart fallback resolution for relative paths
    if (!fs::exists(target_path)) {
        if (fs::exists(fs::path("..") / path)) {
            target_path = fs::path("..") / path;
        } else if (fs::exists(fs::path("../..") / path)) {
            target_path = fs::path("../..") / path;
        }
#if defined(__linux__)
        else {
            std::error_code ec;
            fs::path exe_dir = fs::canonical("/proc/self/exe", ec).parent_path();
            if (!ec) {
                if (fs::exists(exe_dir / path)) {
                    target_path = exe_dir / path;
                } else if (fs::exists(exe_dir / ".." / path)) {
                    target_path = exe_dir / ".." / path;
                }
            }
        }
#endif
    }

    if (!fs::exists(target_path)) {
        out_error = "Target path does not exist: " + path;
        return std::nullopt;
    }

    std::string resolved_str = target_path.string();

    if (fs::is_directory(target_path)) {
        return load_dir(resolved_str, out_error);
    }

    std::string ext = target_path.extension().string();
    if (ext == ".lua") {
        return load_lua(resolved_str, out_error);
    }

    // Default to .gpak / zip archive
    return load_gpak(resolved_str, out_error);
}

std::optional<Cartridge> Cartridge::load_lua(const std::string& path, std::string& out_error) {
    std::ifstream file(path);
    if (!file.is_open()) {
        out_error = "Cannot open Lua script: " + path;
        return std::nullopt;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();

    Cartridge cart;
    cart.m_path = path;
    cart.m_metadata.title = fs::path(path).stem().string();
    cart.m_metadata.entry_point = fs::path(path).filename().string();
    cart.m_code = buffer.str();

    return cart;
}

std::optional<Cartridge> Cartridge::load_dir(const std::string& path, std::string& out_error) {
    fs::path dir_path(path);
    Cartridge cart;
    cart.m_path = path;
    cart.m_metadata.title = dir_path.stem().string();

    // Check for game.json
    fs::path manifest_path = dir_path / "game.json";
    if (fs::exists(manifest_path)) {
        std::ifstream mf(manifest_path);
        if (mf.is_open()) {
            try {
                auto j = nlohmann::json::parse(mf);
                if (j.contains("title")) cart.m_metadata.title = j["title"].get<std::string>();
                if (j.contains("author")) cart.m_metadata.author = j["author"].get<std::string>();
                if (j.contains("version")) cart.m_metadata.version = j["version"].get<std::string>();
                if (j.contains("description")) cart.m_metadata.description = j["description"].get<std::string>();
                if (j.contains("entry")) cart.m_metadata.entry_point = j["entry"].get<std::string>();
            } catch (const std::exception& e) {
                std::cerr << "[Cartridge] Warning: failed to parse game.json: " << e.what() << std::endl;
            }
        }
    }

    fs::path entry_file = dir_path / cart.m_metadata.entry_point;
    if (!fs::exists(entry_file)) {
        out_error = "Entry point not found in directory: " + entry_file.string();
        return std::nullopt;
    }

    std::ifstream file(entry_file);
    if (!file.is_open()) {
        out_error = "Cannot open entry point script: " + entry_file.string();
        return std::nullopt;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    cart.m_code = buffer.str();

    return cart;
}

std::optional<Cartridge> Cartridge::load_gpak(const std::string& path, std::string& out_error) {
    mz_zip_archive zip_archive;
    std::memset(&zip_archive, 0, sizeof(zip_archive));

    mz_bool status = mz_zip_reader_init_file(&zip_archive, path.c_str(), 0);
    if (!status) {
        out_error = "Failed to open .gpak archive file: " + path;
        return std::nullopt;
    }

    Cartridge cart;
    cart.m_path = path;
    cart.m_metadata.title = fs::path(path).stem().string();

    // 1. Try reading game.json if present
    int json_idx = mz_zip_reader_locate_file(&zip_archive, "game.json", nullptr, 0);
    if (json_idx >= 0) {
        size_t uncomp_size = 0;
        void* p = mz_zip_reader_extract_to_heap(&zip_archive, json_idx, &uncomp_size, 0);
        if (p) {
            try {
                std::string json_str(static_cast<const char*>(p), uncomp_size);
                auto j = nlohmann::json::parse(json_str);
                if (j.contains("title")) cart.m_metadata.title = j["title"].get<std::string>();
                if (j.contains("author")) cart.m_metadata.author = j["author"].get<std::string>();
                if (j.contains("version")) cart.m_metadata.version = j["version"].get<std::string>();
                if (j.contains("description")) cart.m_metadata.description = j["description"].get<std::string>();
                if (j.contains("entry")) cart.m_metadata.entry_point = j["entry"].get<std::string>();
            } catch (const std::exception& e) {
                std::cerr << "[Cartridge] Warning: failed to parse game.json in .gpak: " << e.what() << std::endl;
            }
            mz_free(p);
        }
    }

    // 2. Read entry point script
    std::string entry = cart.m_metadata.entry_point.empty() ? "main.lua" : cart.m_metadata.entry_point;
    int code_idx = mz_zip_reader_locate_file(&zip_archive, entry.c_str(), nullptr, 0);

    // Fallback: search for any .lua file if entry wasn't found directly
    if (code_idx < 0) {
        mz_uint num_files = mz_zip_reader_get_num_files(&zip_archive);
        for (mz_uint i = 0; i < num_files; ++i) {
            mz_zip_archive_file_stat file_stat;
            if (mz_zip_reader_file_stat(&zip_archive, i, &file_stat)) {
                std::string fname = file_stat.m_filename;
                if (fname.size() >= 4 && fname.substr(fname.size() - 4) == ".lua") {
                    code_idx = static_cast<int>(i);
                    break;
                }
            }
        }
    }

    if (code_idx < 0) {
        mz_zip_reader_end(&zip_archive);
        out_error = "Entry point (" + entry + ") not found inside .gpak cartridge!";
        return std::nullopt;
    }

    size_t code_size = 0;
    void* code_ptr = mz_zip_reader_extract_to_heap(&zip_archive, code_idx, &code_size, 0);
    if (!code_ptr) {
        mz_zip_reader_end(&zip_archive);
        out_error = "Failed to extract entry script from .gpak cartridge";
        return std::nullopt;
    }

    cart.m_code = std::string(static_cast<const char*>(code_ptr), code_size);
    mz_free(code_ptr);
    mz_zip_reader_end(&zip_archive);

    return cart;
}

} // namespace trayplay
