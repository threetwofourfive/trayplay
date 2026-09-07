#pragma once

#include <cstdint>
#include <array>

namespace trayplay {

// Classic 5x7 ASCII bitmap font (characters 32 ' ' to 126 '~')
// Each character is 5 columns wide, where bit 0 is top and bit 6 is bottom.
// Cell size with spacing: 6x8 pixels.
struct RetroFont {
    static constexpr int GLYPH_WIDTH = 5;
    static constexpr int GLYPH_HEIGHT = 7;
    static constexpr int CELL_WIDTH = 6;
    static constexpr int CELL_HEIGHT = 8;
    static constexpr int FIRST_CHAR = 32;
    static constexpr int NUM_CHARS = 95;

    static const uint8_t* get_glyph(char c) noexcept {
        int idx = static_cast<unsigned char>(c);
        if (idx < FIRST_CHAR || idx >= FIRST_CHAR + NUM_CHARS) {
            idx = '?';
        }
        return DATA[idx - FIRST_CHAR].data();
    }

    // 5-byte column bitmasks for ASCII 32 to 126
    static const std::array<std::array<uint8_t, 5>, NUM_CHARS> DATA;
};

} // namespace trayplay
