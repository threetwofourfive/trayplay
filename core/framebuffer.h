#pragma once

#include <cstdint>
#include <vector>
#include <cstddef>
#include <algorithm>
#include <string>

namespace trayplay {

class Framebuffer {
public:
    static constexpr int WIDTH = 320;
    static constexpr int HEIGHT = 180;
    static constexpr size_t PIXEL_COUNT = WIDTH * HEIGHT;
    static constexpr size_t BUFFER_SIZE_BYTES = PIXEL_COUNT * sizeof(uint32_t);

    Framebuffer();
    ~Framebuffer() = default;

    // Direct pixel access
    void clear(uint32_t color = 0x000000FF);
    void set_pixel(int x, int y, uint32_t color);
    uint32_t get_pixel(int x, int y) const;

    // 2D Drawing Primitives
    void draw_line(int x0, int y0, int x1, int y1, uint32_t color);
    void draw_rect(int x, int y, int w, int h, uint32_t color);
    void fill_rect(int x, int y, int w, int h, uint32_t color);
    void draw_circle(int xc, int yc, int r, uint32_t color);
    void fill_circle(int xc, int yc, int r, uint32_t color);

    // Retro Text Rendering
    void draw_char(int x, int y, char c, uint32_t color, int scale = 1);
    void draw_text(int x, int y, const std::string& text, uint32_t color, int scale = 1);
    int text_width(const std::string& text, int scale = 1) const;
    int text_height(int scale = 1) const;

    // Buffer data pointer access (for Shared Memory / IPC streaming)
    const uint32_t* data() const noexcept { return m_pixels.data(); }
    uint32_t* data() noexcept { return m_pixels.data(); }
    const uint8_t* raw_bytes() const noexcept { return reinterpret_cast<const uint8_t*>(m_pixels.data()); }
    constexpr size_t size_bytes() const noexcept { return BUFFER_SIZE_BYTES; }

    // Color utility: RGBA to packed uint32 (native byte order matching RGBA memory layout)
    static constexpr uint32_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) noexcept {
        return static_cast<uint32_t>(r) |
               (static_cast<uint32_t>(g) << 8) |
               (static_cast<uint32_t>(b) << 16) |
               (static_cast<uint32_t>(a) << 24);
    }

private:
    std::vector<uint32_t> m_pixels;
};

} // namespace trayplay
