#include <iostream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <csignal>
#include <atomic>
#include "framebuffer.h"
#include "vm.h"
#include "cartridge.h"
#include "game_loop.h"
#include "ipc_server.h"

static std::atomic<bool> g_shutdown_requested{false};

static void signal_handler(int sig) {
    std::cout << "[Core] Signal received: " << sig << std::endl;
    g_shutdown_requested.store(true);
}

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    std::signal(SIGHUP, SIG_IGN);
    std::signal(SIGPIPE, SIG_IGN);

    std::cout << "========================================" << std::endl;
    std::cout << "   TrayPlay Microconsole Core Daemon   " << std::endl;
    std::cout << "========================================" << std::endl;

    trayplay::Framebuffer fb;
    trayplay::VM vm(fb);
    trayplay::GameLoop game_loop(fb, vm);
    trayplay::IpcServer ipc(game_loop, vm, fb);

    // Setup frame publish hook for Zero-Copy Shared Memory streaming
    game_loop.set_on_frame([&ipc](const trayplay::Framebuffer& f, const trayplay::GameTelemetry& telem) {
        ipc.publish_frame(f, telem);
    });

    if (!ipc.start()) {
        std::cerr << "[Core ERROR] Failed to start IPC server!" << std::endl;
        return 1;
    }

    // Optional cartridge on startup (only if explicitly passed as argument)
    if (argc > 1) {
        std::string cart_path = argv[1];
        std::string load_error;
        auto cart_opt = trayplay::Cartridge::load(cart_path, load_error);

        if (cart_opt) {
            std::cout << "[Core] Loading startup cartridge: " << cart_path << std::endl;
            if (game_loop.load_cartridge(*cart_opt)) {
                ipc.set_cartridge_title(cart_opt->get_metadata().title);
                std::cout << "[Core] Started: " << cart_opt->get_metadata().title << " (60 FPS)" << std::endl;
            }
        } else {
            std::cerr << "[Core] Warning: failed to load " << cart_path << ": " << load_error << std::endl;
        }
    } else {
        std::cout << "[Core] No startup cartridge specified. Daemon idling in WAITING state..." << std::endl;
    }

    // Start background game loop thread
    std::thread loop_thread([&game_loop]() {
        game_loop.run();
    });

    std::cout << "[Core] Daemon running. Press Ctrl+C or send 'quit' command via IPC to exit." << std::endl;

    // Monitor for shutdown signal or IPC quit
    while (!g_shutdown_requested.load() && ipc.is_running()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "[Core] Shutting down daemon..." << std::endl;
    game_loop.stop();
    if (loop_thread.joinable()) {
        loop_thread.join();
    }

    ipc.stop();
    std::cout << "[Core] Daemon exited cleanly." << std::endl;
    return 0;
}
