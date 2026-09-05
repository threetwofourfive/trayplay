#include <iostream>
#include <iomanip>
#include "framebuffer.h"
#include "vm.h"

int main(int argc, char* argv[]) {
    std::cout << "========================================" << std::endl;
    std::cout << "  TrayPlay Microconsole Core (Daemon)   " << std::endl;
    std::cout << "========================================" << std::endl;

    trayplay::Framebuffer fb;
    trayplay::VM vm(fb);

    // Demonstration / test Lua script
    const std::string demo_script = R"(
        function init()
            print("[Lua] Cartridge initialized successfully!")
        end

        function update(dt)
            -- Logic update tick
        end

        function draw()
            -- 1. Clear background to dark blue (palette index 1)
            cls(1)

            -- 2. Draw a red rectangle (palette index 8)
            rectfill(20, 20, 80, 40, 8)

            -- 3. Draw a green circle (palette index 11) in the center
            circfill(160, 90, 25, 11)

            -- 4. Draw a yellow diagonal line (palette index 10)
            line(0, 0, 319, 179, 10)
        end
    )";

    std::cout << "[Core] Loading embedded test Lua script..." << std::endl;
    if (!vm.load_script(demo_script)) {
        std::cerr << "[Core ERROR] Failed to load script: " << vm.get_last_error() << std::endl;
        return 1;
    }

    std::cout << "[Core] Calling init()..." << std::endl;
    vm.init();

    std::cout << "[Core] Calling update(1/60)..." << std::endl;
    vm.update(1.0 / 60.0);

    std::cout << "[Core] Calling draw()..." << std::endl;
    vm.draw();

    // Verify drawn pixels
    uint32_t bg_pixel = fb.get_pixel(5, 5);
    uint32_t rect_pixel = fb.get_pixel(25, 25);
    uint32_t circle_pixel = fb.get_pixel(160, 90);
    uint32_t line_pixel = fb.get_pixel(0, 0);

    std::cout << "[Core] Verification of Framebuffer pixels:" << std::endl;
    std::cout << "  - Background (0,0): 0x" << std::hex << std::setw(8) << std::setfill('0') << bg_pixel << std::dec << " (should be dark blue)" << std::endl;
    std::cout << "  - Inside rect (25,25): 0x" << std::hex << std::setw(8) << std::setfill('0') << rect_pixel << std::dec << " (should be red)" << std::endl;
    std::cout << "  - Center circle (160,90): 0x" << std::hex << std::setw(8) << std::setfill('0') << circle_pixel << std::dec << " (should be green)" << std::endl;
    std::cout << "  - Line start (0,0): 0x" << std::hex << std::setw(8) << std::setfill('0') << line_pixel << std::dec << " (should be yellow)" << std::endl;

    std::cout << "[Core] All systems nominal! Framebuffer & Lua VM operational." << std::endl;
    return 0;
}
