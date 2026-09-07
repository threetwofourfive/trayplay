#include "vm.h"
#include "audio.h"
#include <sol/sol.hpp>
#include <fstream>
#include <sstream>
#include <iostream>

namespace trayplay {

VM::VM(Framebuffer& framebuffer, Audio* audio)
    : m_framebuffer(framebuffer),
      m_audio(audio),
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

    lua["pset"] = [this](double x, double y, sol::optional<uint32_t> col) {
        uint32_t raw_col = col.value_or(7); // Default white
        m_framebuffer.set_pixel(static_cast<int>(std::round(x)), static_cast<int>(std::round(y)), resolve_color(raw_col));
    };

    lua["pget"] = [this](double x, double y) -> uint32_t {
        return m_framebuffer.get_pixel(static_cast<int>(std::round(x)), static_cast<int>(std::round(y)));
    };

    lua["line"] = [this](double x0, double y0, double x1, double y1, sol::optional<uint32_t> col) {
        uint32_t raw_col = col.value_or(7);
        m_framebuffer.draw_line(
            static_cast<int>(std::round(x0)), static_cast<int>(std::round(y0)),
            static_cast<int>(std::round(x1)), static_cast<int>(std::round(y1)),
            resolve_color(raw_col)
        );
    };

    lua["rect"] = [this](double x, double y, double w, double h, sol::optional<uint32_t> col) {
        uint32_t raw_col = col.value_or(7);
        m_framebuffer.draw_rect(
            static_cast<int>(std::round(x)), static_cast<int>(std::round(y)),
            static_cast<int>(std::round(w)), static_cast<int>(std::round(h)),
            resolve_color(raw_col)
        );
    };

    lua["rectfill"] = [this](double x, double y, double w, double h, sol::optional<uint32_t> col) {
        uint32_t raw_col = col.value_or(7);
        m_framebuffer.fill_rect(
            static_cast<int>(std::round(x)), static_cast<int>(std::round(y)),
            static_cast<int>(std::round(w)), static_cast<int>(std::round(h)),
            resolve_color(raw_col)
        );
    };

    lua["circ"] = [this](double xc, double yc, double r, sol::optional<uint32_t> col) {
        uint32_t raw_col = col.value_or(7);
        m_framebuffer.draw_circle(
            static_cast<int>(std::round(xc)), static_cast<int>(std::round(yc)),
            static_cast<int>(std::round(r)),
            resolve_color(raw_col)
        );
    };

    lua["circfill"] = [this](double xc, double yc, double r, sol::optional<uint32_t> col) {
        uint32_t raw_col = col.value_or(7);
        m_framebuffer.fill_circle(
            static_cast<int>(std::round(xc)), static_cast<int>(std::round(yc)),
            static_cast<int>(std::round(r)),
            resolve_color(raw_col)
        );
    };

    // Text rendering API
    auto stringify = [this](sol::object obj) -> std::string {
        if (obj.is<std::string>()) return obj.as<std::string>();
        sol::protected_function tostring_fn = (*m_lua)["tostring"];
        if (tostring_fn.valid()) {
            auto res = tostring_fn(obj);
            if (res.valid()) return res.get<std::string>();
        }
        return "";
    };

    lua["text"] = [this, stringify](sol::object obj, double x, double y, sol::optional<uint32_t> col, sol::optional<int> scale) {
        std::string str = stringify(obj);
        uint32_t raw_col = col.value_or(7);
        m_framebuffer.draw_text(
            static_cast<int>(std::round(x)),
            static_cast<int>(std::round(y)),
            str,
            resolve_color(raw_col),
            scale.value_or(1)
        );
    };

    lua["print"] = [this, stringify](sol::object obj, sol::optional<double> x, sol::optional<double> y, sol::optional<uint32_t> col, sol::optional<int> scale) {
        std::string str = stringify(obj);
        double px = x.value_or(0.0);
        double py = y.value_or(0.0);
        uint32_t raw_col = col.value_or(7);
        m_framebuffer.draw_text(
            static_cast<int>(std::round(px)),
            static_cast<int>(std::round(py)),
            str,
            resolve_color(raw_col),
            scale.value_or(1)
        );
    };

    lua["text_width"] = [this, stringify](sol::object obj, sol::optional<int> scale) -> int {
        std::string str = stringify(obj);
        return m_framebuffer.text_width(str, scale.value_or(1));
    };

    // Input API
    lua["btn"] = [this](const std::string& name) -> bool {
        return get_button(name);
    };

    // Chiptune Audio API
    sol::table waves = lua.create_table();
    waves["PULSE"] = static_cast<int>(Waveform::PULSE);
    waves["SQUARE"] = static_cast<int>(Waveform::PULSE);
    waves["TRIANGLE"] = static_cast<int>(Waveform::TRIANGLE);
    waves["NOISE"] = static_cast<int>(Waveform::NOISE);
    waves["SINE"] = static_cast<int>(Waveform::SINE);
    waves["SAWTOOTH"] = static_cast<int>(Waveform::SAWTOOTH);
    lua["WAVE"] = waves;

    lua["beep"] = [this](sol::optional<double> freq, sol::optional<double> duration_ms, sol::optional<double> volume, sol::optional<int> wave) {
        if (!m_audio) return;
        float f = static_cast<float>(freq.value_or(440.0));
        float d = static_cast<float>(duration_ms.value_or(100.0));
        float v = static_cast<float>(volume.value_or(0.5));
        int w = wave.value_or(0);
        m_audio->beep(f, d, v, static_cast<Waveform>(w));
    };

    lua["sfx"] = [this](sol::object param) {
        if (!m_audio) return;
        if (param.is<std::string>()) {
            m_audio->play_sfx(param.as<std::string>());
        }
    };

    lua["tone"] = [this](int channel, double freq, double duration_ms, sol::optional<double> volume, sol::optional<int> wave, sol::optional<double> sweep) {
        if (!m_audio) return;
        m_audio->tone(
            channel,
            static_cast<float>(freq),
            static_cast<float>(duration_ms),
            static_cast<float>(volume.value_or(0.5)),
            static_cast<Waveform>(wave.value_or(0)),
            static_cast<float>(sweep.value_or(0.0))
        );
    };

    lua["stop_audio"] = [this](sol::optional<int> channel) {
        if (!m_audio) return;
        if (channel.has_value()) {
            m_audio->stop_channel(channel.value());
        } else {
            m_audio->stop();
        }
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
