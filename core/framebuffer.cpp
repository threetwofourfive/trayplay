#include "framebuffer.h"
#include "font.h"
#include <cmath>
#include <cstdlib>

namespace trayplay {

Framebuffer::Framebuffer()
    : m_pixels(PIXEL_COUNT, 0x000000FF) {}

void Framebuffer::clear(uint32_t color) {
    std::fill(m_pixels.begin(), m_pixels.end(), color);
}

void Framebuffer::set_pixel(int x, int y, uint32_t color) {
    if (x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT) {
        m_pixels[static_cast<size_t>(y * WIDTH + x)] = color;
    }
}

uint32_t Framebuffer::get_pixel(int x, int y) const {
    if (x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT) {
        return m_pixels[static_cast<size_t>(y * WIDTH + x)];
    }
    return 0;
}

void Framebuffer::draw_line(int x0, int y0, int x1, int y1, uint32_t color) {
    // Bresenham's line algorithm
    int dx = std::abs(x1 - x0);
    int dy = -std::abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;

    while (true) {
        set_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void Framebuffer::draw_rect(int x, int y, int w, int h, uint32_t color) {
    if (w <= 0 || h <= 0) return;
    int right = x + w - 1;
    int bottom = y + h - 1;

    // Horizontal lines
    for (int i = x; i <= right; ++i) {
        set_pixel(i, y, color);
        set_pixel(i, bottom, color);
    }
    // Vertical lines
    for (int j = y; j <= bottom; ++j) {
        set_pixel(x, j, color);
        set_pixel(right, j, color);
    }
}

void Framebuffer::fill_rect(int x, int y, int w, int h, uint32_t color) {
    if (w <= 0 || h <= 0) return;

    int x0 = std::max(0, x);
    int y0 = std::max(0, y);
    int x1 = std::min(WIDTH, x + w);
    int y1 = std::min(HEIGHT, y + h);

    for (int py = y0; py < y1; ++py) {
        size_t row_start = static_cast<size_t>(py * WIDTH);
        for (int px = x0; px < x1; ++px) {
            m_pixels[row_start + static_cast<size_t>(px)] = color;
        }
    }
}

void Framebuffer::draw_circle(int xc, int yc, int r, uint32_t color) {
    if (r < 0) return;
    int x = 0;
    int y = r;
    int d = 3 - 2 * r;

    auto plot8 = [this, xc, yc, color](int x, int y) {
        set_pixel(xc + x, yc + y, color);
        set_pixel(xc - x, yc + y, color);
        set_pixel(xc + x, yc - y, color);
        set_pixel(xc - x, yc - y, color);
        set_pixel(xc + y, yc + x, color);
        set_pixel(xc - y, yc + x, color);
        set_pixel(xc + y, yc - x, color);
        set_pixel(xc - y, yc - x, color);
    };

    plot8(x, y);
    while (y >= x) {
        x++;
        if (d > 0) {
            y--;
            d = d + 4 * (x - y) + 10;
        } else {
            d = d + 4 * x + 6;
        }
        plot8(x, y);
    }
}

void Framebuffer::fill_circle(int xc, int yc, int r, uint32_t color) {
    if (r < 0) return;
    int x = 0;
    int y = r;
    int d = 3 - 2 * r;

    auto draw_hline = [this, color](int x1, int x2, int y) {
        if (x1 > x2) std::swap(x1, x2);
        x1 = std::max(0, x1);
        x2 = std::min(WIDTH - 1, x2);
        if (y < 0 || y >= HEIGHT) return;
        size_t row_start = static_cast<size_t>(y * WIDTH);
        for (int x = x1; x <= x2; ++x) {
            m_pixels[row_start + static_cast<size_t>(x)] = color;
        }
    };

    while (y >= x) {
        draw_hline(xc - x, xc + x, yc - y);
        draw_hline(xc - x, xc + x, yc + y);
        draw_hline(xc - y, xc + y, yc - x);
        draw_hline(xc - y, xc + y, yc + x);

        x++;
        if (d > 0) {
            y--;
            d = d + 4 * (x - y) + 10;
        } else {
            d = d + 4 * x + 6;
        }
    }
}

void Framebuffer::draw_char(int x, int y, char c, uint32_t color, int scale) {
    if (scale < 1) scale = 1;
    const uint8_t* glyph = RetroFont::get_glyph(c);

    if (scale == 1) {
        for (int col = 0; col < RetroFont::GLYPH_WIDTH; ++col) {
            uint8_t col_mask = glyph[col];
            for (int row = 0; row < RetroFont::GLYPH_HEIGHT; ++row) {
                if (col_mask & (1 << row)) {
                    set_pixel(x + col, y + row, color);
                }
            }
        }
    } else {
        for (int col = 0; col < RetroFont::GLYPH_WIDTH; ++col) {
            uint8_t col_mask = glyph[col];
            for (int row = 0; row < RetroFont::GLYPH_HEIGHT; ++row) {
                if (col_mask & (1 << row)) {
                    fill_rect(x + col * scale, y + row * scale, scale, scale, color);
                }
            }
        }
    }
}

void Framebuffer::draw_text(int x, int y, const std::string& text, uint32_t color, int scale) {
    if (scale < 1) scale = 1;
    int cur_x = x;
    int cur_y = y;

    for (char c : text) {
        if (c == '\n') {
            cur_x = x;
            cur_y += RetroFont::CELL_HEIGHT * scale;
            continue;
        }
        if (c == '\r') {
            continue;
        }
        draw_char(cur_x, cur_y, c, color, scale);
        cur_x += RetroFont::CELL_WIDTH * scale;
    }
}

int Framebuffer::text_width(const std::string& text, int scale) const {
    if (scale < 1) scale = 1;
    int max_len = 0;
    int cur_len = 0;
    for (char c : text) {
        if (c == '\n') {
            if (cur_len > max_len) max_len = cur_len;
            cur_len = 0;
        } else if (c != '\r') {
            cur_len++;
        }
    }
    if (cur_len > max_len) max_len = cur_len;
    return max_len * RetroFont::CELL_WIDTH * scale;
}

int Framebuffer::text_height(int scale) const {
    if (scale < 1) scale = 1;
    return RetroFont::CELL_HEIGHT * scale;
}

} // namespace trayplay
