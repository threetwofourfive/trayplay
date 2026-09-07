#include "game_loop.h"
#include "audio.h"
#include <thread>
#include <iostream>

namespace trayplay {

GameLoop::GameLoop(Framebuffer& framebuffer, VM& vm)
    : m_framebuffer(framebuffer), m_vm(vm) {
}

GameLoop::~GameLoop() {
    stop();
}

bool GameLoop::load_cartridge(const Cartridge& cartridge) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_should_exit.store(false);

    if (!cartridge.is_valid()) {
        m_last_error = "Invalid cartridge code";
        m_state.store(GameState::CRASHED);
        return false;
    }

    if (!m_vm.load_script(cartridge.get_code())) {
        m_last_error = m_vm.get_last_error();
        m_state.store(GameState::CRASHED);
        return false;
    }

    m_vm.init();
    if (m_vm.has_error()) {
        m_last_error = m_vm.get_last_error();
        m_state.store(GameState::CRASHED);
        return false;
    }

    m_accumulator = 0.0;
    m_last_time = std::chrono::steady_clock::now();
    m_state.store(GameState::RUNNING);
    return true;
}

void GameLoop::unload_cartridge() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (auto* a = m_vm.get_audio()) {
        a->stop();
    }
    m_framebuffer.clear(0x000000FF);
    m_telemetry.fps = 0.0;
    m_state.store(GameState::STOPPED);
    if (m_on_frame) {
        m_on_frame(m_framebuffer, m_telemetry);
    }
}

void GameLoop::start() {
    if (m_state.load() == GameState::PAUSED) {
        m_last_time = std::chrono::steady_clock::now();
        m_state.store(GameState::RUNNING);
    }
}

void GameLoop::pause() {
    if (m_state.load() == GameState::RUNNING) {
        if (auto* a = m_vm.get_audio()) {
            a->stop();
        }
        m_state.store(GameState::PAUSED);
    }
}

void GameLoop::resume() {
    start();
}

void GameLoop::stop() {
    m_should_exit.store(true);
    if (auto* a = m_vm.get_audio()) {
        a->stop();
    }
    if (m_state.load() != GameState::CRASHED) {
        m_state.store(GameState::STOPPED);
    }
}

void GameLoop::step(double dt) {
    if (m_state.load() != GameState::RUNNING && m_state.load() != GameState::PAUSED) {
        return;
    }

    m_vm.update(dt);
    if (m_vm.has_error()) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_last_error = m_vm.get_last_error();
        m_state.store(GameState::CRASHED);
        return;
    }

    m_vm.draw();
    if (m_vm.has_error()) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_last_error = m_vm.get_last_error();
        m_state.store(GameState::CRASHED);
        return;
    }

    if (m_on_frame) {
        m_on_frame(m_framebuffer, m_telemetry);
    }
}

void GameLoop::tick() {
    if (m_state.load() != GameState::RUNNING) {
        return;
    }

    auto now = std::chrono::steady_clock::now();
    double frame_time = std::chrono::duration<double>(now - m_last_time).count();
    m_last_time = now;

    // Prevent spiral of death on long frame delays
    if (frame_time > 0.25) {
        frame_time = 0.25;
    }

    m_accumulator += frame_time;
    while (m_accumulator >= TARGET_DT) {
        m_vm.update(TARGET_DT);
        if (m_vm.has_error()) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_last_error = m_vm.get_last_error();
            m_state.store(GameState::CRASHED);
            return;
        }
        m_accumulator -= TARGET_DT;
    }

    m_vm.draw();
    if (m_vm.has_error()) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_last_error = m_vm.get_last_error();
        m_state.store(GameState::CRASHED);
        return;
    }

    update_telemetry(frame_time);

    if (m_on_frame) {
        m_on_frame(m_framebuffer, m_telemetry);
    }
}

void GameLoop::run() {
    m_should_exit.store(false);
    m_last_time = std::chrono::steady_clock::now();

    while (!m_should_exit.load()) {
        if (m_state.load() == GameState::RUNNING) {
            auto start_tick = std::chrono::steady_clock::now();
            tick();
            auto end_tick = std::chrono::steady_clock::now();

            auto elapsed = std::chrono::duration<double>(end_tick - start_tick).count();
            double sleep_needed = TARGET_DT - elapsed;
            if (sleep_needed > 0.001) {
                std::this_thread::sleep_for(std::chrono::duration<double>(sleep_needed));
            }
        } else {
            // Idle sleep when paused or stopped to preserve battery / CPU
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
}

void GameLoop::update_telemetry(double frame_time_sec) {
    m_telemetry.total_frames++;
    m_telemetry.frame_time_ms = frame_time_sec * 1000.0;
    if (frame_time_sec > 0.0) {
        double instant_fps = 1.0 / frame_time_sec;
        if (m_telemetry.fps == 0.0) {
            m_telemetry.fps = instant_fps;
        } else {
            m_telemetry.fps = m_telemetry.fps * 0.9 + instant_fps * 0.1;
        }
    }
}

const std::string& GameLoop::get_last_error() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_last_error;
}

} // namespace trayplay
