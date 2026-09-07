#pragma once

#include <string>
#include <unordered_map>
#include <array>
#include <cstdint>
#include <memory>
#include "framebuffer.h"

// Forward declaration of sol::state
namespace sol {
    class state;
}

namespace trayplay {

class Audio;

class VM {
public:
    explicit VM(Framebuffer& framebuffer, Audio* audio = nullptr);
    ~VM();

    void set_audio(Audio* audio) noexcept { m_audio = audio; }
    Audio* get_audio() const noexcept { return m_audio; }

    // Prevent copying
    VM(const VM&) = delete;
    VM& operator=(const VM&) = delete;

    // Script lifecycle
    bool load_script(const std::string& code);
    bool load_file(const std::string& file_path);

    void init();
    void update(double dt = 1.0 / 60.0);
    void draw();

    // Input state management
    void set_button(const std::string& name, bool pressed);
    bool get_button(const std::string& name) const;
    void reset_input();

    // Diagnostics
    bool has_error() const noexcept { return !m_last_error.empty(); }
    const std::string& get_last_error() const noexcept { return m_last_error; }
    void clear_error() noexcept { m_last_error.clear(); }

    // Palette: Classic 16-color fantasy console palette
    static constexpr std::array<uint32_t, 16> PALETTE = {
        Framebuffer::rgba(0, 0, 0),        // 0: Black
        Framebuffer::rgba(29, 43, 83),     // 1: Dark Blue
        Framebuffer::rgba(126, 37, 83),    // 2: Dark Purple
        Framebuffer::rgba(0, 135, 81),     // 3: Dark Green
        Framebuffer::rgba(171, 82, 54),    // 4: Brown
        Framebuffer::rgba(95, 87, 79),     // 5: Dark Gray
        Framebuffer::rgba(194, 195, 199),  // 6: Light Gray
        Framebuffer::rgba(255, 241, 232),  // 7: White
        Framebuffer::rgba(255, 0, 77),     // 8: Red
        Framebuffer::rgba(255, 163, 0),    // 9: Orange
        Framebuffer::rgba(255, 236, 39),   // 10: Yellow
        Framebuffer::rgba(0, 228, 54),     // 11: Green
        Framebuffer::rgba(41, 173, 255),   // 12: Blue
        Framebuffer::rgba(131, 118, 156),  // 13: Indigo
        Framebuffer::rgba(255, 119, 168),  // 14: Pink
        Framebuffer::rgba(255, 204, 170)   // 15: Peach
    };

    static uint32_t resolve_color(uint32_t color_or_index) noexcept {
        if (color_or_index < 16) {
            return PALETTE[color_or_index];
        }
        return color_or_index;
    }

private:
    void register_api();

    Framebuffer& m_framebuffer;
    Audio* m_audio{nullptr};
    std::unique_ptr<sol::state> m_lua;
    std::unordered_map<std::string, bool> m_buttons;
    std::string m_last_error;

    bool m_has_init{false};
    bool m_has_update{false};
    bool m_has_draw{false};
};

} // namespace trayplay
