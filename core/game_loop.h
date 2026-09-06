#pragma once

#include <string>
#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include "framebuffer.h"
#include "vm.h"
#include "cartridge.h"

namespace trayplay {

enum class GameState {
    STOPPED,
    RUNNING,
    PAUSED,
    CRASHED
};

struct GameTelemetry {
    double fps = 0.0;
    double frame_time_ms = 0.0;
    uint64_t total_frames = 0;
};

class GameLoop {
public:
    using FrameCallback = std::function<void(const Framebuffer&, const GameTelemetry&)>;

    GameLoop(Framebuffer& framebuffer, VM& vm);
    ~GameLoop();

    // Prevent copying
    GameLoop(const GameLoop&) = delete;
    GameLoop& operator=(const GameLoop&) = delete;

    // Cartridge loading
    bool load_cartridge(const Cartridge& cartridge);
    void unload_cartridge();

    // Execution control
    void start();
    void pause();
    void resume();
    void stop();

    // Execute exactly one tick / step (can be driven externally or used for deterministic testing)
    void tick();
    void step(double dt);

    // Continuous loop (runs on calling thread until stop() is called)
    void run();

    // Frame rendered callback (e.g. for IPC / Shared Memory notify)
    void set_on_frame(FrameCallback cb) { m_on_frame = std::move(cb); }

    // State queries
    GameState get_state() const noexcept { return m_state.load(); }
    const GameTelemetry& get_telemetry() const noexcept { return m_telemetry; }
    const std::string& get_last_error() const;
    bool is_running() const noexcept { return m_state.load() == GameState::RUNNING; }
    bool is_paused() const noexcept { return m_state.load() == GameState::PAUSED; }

private:
    void update_telemetry(double frame_time_sec);

    Framebuffer& m_framebuffer;
    VM& m_vm;

    std::atomic<GameState> m_state{GameState::STOPPED};
    std::atomic<bool> m_should_exit{false};

    GameTelemetry m_telemetry;
    std::string m_last_error;
    mutable std::mutex m_mutex;

    FrameCallback m_on_frame;

    // Fixed timestep accumulator
    std::chrono::steady_clock::time_point m_last_time;
    double m_accumulator{0.0};
    static constexpr double TARGET_DT = 1.0 / 60.0; // 60 FPS fixed timestep
};

} // namespace trayplay
