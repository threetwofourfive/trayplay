#pragma once

#include <string>
#include <memory>
#include <atomic>
#include <thread>
#include <cstdint>
#include <mutex>
#include "framebuffer.h"
#include "game_loop.h"
#include "vm.h"

namespace trayplay {

#pragma pack(push, 1)
struct SharedFrameHeader {
    uint32_t magic;         // 0x54524159 ('TRAY')
    uint32_t version;       // Protocol version: 1
    uint32_t width;         // 320
    uint32_t height;        // 180
    uint32_t pitch;         // 320 * 4 = 1280 bytes
    uint64_t frame_index;   // Incremented sequentially on every new frame
    uint32_t state;         // 0: STOPPED, 1: RUNNING, 2: PAUSED, 3: CRASHED
    float fps;              // Smoothed FPS
    char title[64];         // Current cartridge title
};
#pragma pack(pop)

class IpcServer {
public:
    static constexpr const char* DEFAULT_SHM_NAME = "trayplay_framebuffer";
    static constexpr const char* DEFAULT_SOCKET_PATH = "/tmp/trayplay.sock";

    IpcServer(GameLoop& game_loop, VM& vm, Framebuffer& framebuffer,
              const std::string& socket_path = DEFAULT_SOCKET_PATH,
              const std::string& shm_name = DEFAULT_SHM_NAME);
    ~IpcServer();

    // Prevent copying
    IpcServer(const IpcServer&) = delete;
    IpcServer& operator=(const IpcServer&) = delete;

    bool start();
    void stop();

    // Update shared memory frame
    void publish_frame(const Framebuffer& fb, const GameTelemetry& telemetry);
    void set_cartridge_title(const std::string& title);

    bool is_running() const noexcept { return m_running.load(); }

private:
    void init_shm();
    void cleanup_shm();

    void socket_server_thread();
    void handle_client(int client_fd);
    std::string process_command(const std::string& json_str);

    GameLoop& m_game_loop;
    VM& m_vm;
    Framebuffer& m_framebuffer;

    std::string m_socket_path;
    std::string m_shm_name;

    std::atomic<bool> m_running{false};
    std::thread m_server_thread;
    int m_server_fd{-1};

    // Shared memory mapping pointers
    void* m_shm_ptr{nullptr};
    size_t m_shm_size{0};
    int m_shm_fd{-1};
    uint64_t m_frame_counter{0};
    std::string m_current_title{"No Cartridge"};
    std::mutex m_title_mutex;
};

} // namespace trayplay
