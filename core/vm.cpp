#include "vm.h"
#include <sol/sol.hpp>
#include <fstream>
#include <sstream>
#include <iostream>

namespace trayplay {

VM::VM(Framebuffer& framebuffer)
    : m_framebuffer(framebuffer),
      m_lua(std::make_unique<sol::state>()) {
    
    // Open safe standard libraries for the sandbox
    // Exclude os and io to prevent untrusted cartridge scripts from harming host system
    m_lua->open_libraries(
        sol::lib::base,
        sol::lib::math,
        sol::lib::string,
        sol::lib::table
    );

    register_api();
}

VM::~VM() = default;

void VM::register_api() {
    auto& lua = *m_lua;

    // Screen dimension constants
    lua["WIDTH"] = Framebuffer::WIDTH;
    lua["HEIGHT"] = Framebuffer::HEIGHT;

    // Color constants table
    sol::table colors = lua.create_table();
    colors["BLACK"] = 0;
    colors["DARK_BLUE"] = 1;
    colors["DARK_PURPLE"] = 2;
    colors["DARK_GREEN"] = 3;
    colors["BROWN"] = 4;
    colors["DARK_GRAY"] = 5;
    colors["LIGHT_GRAY"] = 6;
    colors["WHITE"] = 7;
    colors["RED"] = 8;
    colors["ORANGE"] = 9;
    colors["YELLOW"] = 10;
    colors["GREEN"] = 11;
    colors["BLUE"] = 12;
    colors["INDIGO"] = 13;
    colors["PINK"] = 14;
    colors["PEACH"] = 15;
    lua["COLOR"] = colors;

    // Color constructor utilities
    lua["rgba"] = [](uint8_t r, uint8_t g, uint8_t b, sol::optional<uint8_t> a) -> uint32_t {
        return Framebuffer::rgba(r, g, b, a.value_or(255));
    };

    lua["rgb"] = [](uint8_t r, uint8_t g, uint8_t b) -> uint32_t {
        return Framebuffer::rgba(r, g, b, 255);
    };

    // Drawing primitives
    lua["cls"] = [this](sol::optional<uint32_t> col) {
        uint32_t raw_col = col.value_or(0);
        m_framebuffer.clear(resolve_color(raw_col));
    };

    lua["pset"] = [this](int x, int y, sol::optional<uint32_t> col) {
        uint32_t raw_col = col.value_or(7); // Default white
        m_framebuffer.set_pixel(x, y, resolve_color(raw_col));
    };

    lua["pget"] = [this](int x, int y) -> uint32_t {
        return m_framebuffer.get_pixel(x, y);
    };

    lua["line"] = [this](int x0, int y0, int x1, int y1, sol::optional<uint32_t> col) {
        uint32_t raw_col = col.value_or(7);
        m_framebuffer.draw_line(x0, y0, x1, y1, resolve_color(raw_col));
    };

    lua["rect"] = [this](int x, int y, int w, int h, sol::optional<uint32_t> col) {
        uint32_t raw_col = col.value_or(7);
        m_framebuffer.draw_rect(x, y, w, h, resolve_color(raw_col));
    };

    lua["rectfill"] = [this](int x, int y, int w, int h, sol::optional<uint32_t> col) {
        uint32_t raw_col = col.value_or(7);
        m_framebuffer.fill_rect(x, y, w, h, resolve_color(raw_col));
    };

    lua["circ"] = [this](int xc, int yc, int r, sol::optional<uint32_t> col) {
        uint32_t raw_col = col.value_or(7);
        m_framebuffer.draw_circle(xc, yc, r, resolve_color(raw_col));
    };

    lua["circfill"] = [this](int xc, int yc, int r, sol::optional<uint32_t> col) {
        uint32_t raw_col = col.value_or(7);
        m_framebuffer.fill_circle(xc, yc, r, resolve_color(raw_col));
    };

    // Input API
    lua["btn"] = [this](const std::string& name) -> bool {
        return get_button(name);
    };
}

bool VM::load_script(const std::string& code) {
    clear_error();
    try {
        sol::protected_function_result result = m_lua->safe_script(code);
        if (!result.valid()) {
            sol::error err = result;
            m_last_error = err.what();
            std::cerr << "[TrayPlay VM Error] " << m_last_error << std::endl;
            return false;
        }
    } catch (const std::exception& e) {
        m_last_error = e.what();
        std::cerr << "[TrayPlay VM Exception] " << m_last_error << std::endl;
        return false;
    }

    // Inspect lifecycle hooks defined in the script
    sol::object init_obj = (*m_lua)["init"];
    sol::object update_obj = (*m_lua)["update"];
    sol::object draw_obj = (*m_lua)["draw"];

    m_has_init = init_obj.is<sol::protected_function>();
    m_has_update = update_obj.is<sol::protected_function>();
    m_has_draw = draw_obj.is<sol::protected_function>();

    return true;
}

bool VM::load_file(const std::string& file_path) {
    std::ifstream file(file_path);
    if (!file.is_open()) {
        m_last_error = "Failed to open script file: " + file_path;
        return false;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return load_script(buffer.str());
}

void VM::init() {
    if (!m_has_init) return;
    sol::protected_function init_fn = (*m_lua)["init"];
    sol::protected_function_result result = init_fn();
    if (!result.valid()) {
        sol::error err = result;
        m_last_error = err.what();
        std::cerr << "[TrayPlay VM Error in init()] " << m_last_error << std::endl;
    }
}

void VM::update(double dt) {
    if (!m_has_update) return;
    sol::protected_function update_fn = (*m_lua)["update"];
    sol::protected_function_result result = update_fn(dt);
    if (!result.valid()) {
        sol::error err = result;
        m_last_error = err.what();
        std::cerr << "[TrayPlay VM Error in update()] " << m_last_error << std::endl;
    }
}

void VM::draw() {
    if (!m_has_draw) return;
    sol::protected_function draw_fn = (*m_lua)["draw"];
    sol::protected_function_result result = draw_fn();
    if (!result.valid()) {
        sol::error err = result;
        m_last_error = err.what();
        std::cerr << "[TrayPlay VM Error in draw()] " << m_last_error << std::endl;
    }
}

void VM::set_button(const std::string& name, bool pressed) {
    m_buttons[name] = pressed;
}

bool VM::get_button(const std::string& name) const {
    auto it = m_buttons.find(name);
    if (it != m_buttons.end()) {
        return it->second;
    }
    return false;
}

void VM::reset_input() {
    m_buttons.clear();
}

} // namespace trayplay
