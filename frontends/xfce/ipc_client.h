#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <gio/gio.h>
#include <sys/types.h>

namespace trayplay::xfce {

enum class DaemonState : uint32_t {
    STOPPED = 0,
    RUNNING = 1,
    PAUSED = 2,
    CRASHED = 3,
};

#pragma pack(push, 1)
struct SharedFrameHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint64_t frame_index;
    uint32_t state;
    float fps;
    char title[64];
};
#pragma pack(pop)

static_assert(sizeof(SharedFrameHeader) == 100, "SharedFrameHeader must remain 100 bytes");

struct FrameSnapshot {
    bool shm_available = false;
    bool new_frame = false;
    uint64_t frame_index = 0;
    DaemonState state = DaemonState::STOPPED;
    float fps = 0.0f;
    std::string title;
};

struct IpcResponse {
    bool transport_ok = false;
    bool ok = false;
    std::string message;
    std::string title;
};

class IpcClient {
public:
    static constexpr const char* SHM_PATH = "/dev/shm/trayplay_framebuffer";
    static constexpr const char* SOCKET_PATH = "/tmp/trayplay.sock";
    static constexpr const char* PID_PATH = "/tmp/trayplay.pid";
    static constexpr uint32_t SHM_MAGIC = 0x54524159; // 'TRAY'
    static constexpr uint32_t SHM_VERSION = 1;
    static constexpr int VIRTUAL_WIDTH = 320;
    static constexpr int VIRTUAL_HEIGHT = 180;
    static constexpr size_t PIXEL_BUFFER_SIZE = VIRTUAL_WIDTH * VIRTUAL_HEIGHT * 4;

    IpcClient();
    ~IpcClient();

    IpcClient(const IpcClient&) = delete;
    IpcClient& operator=(const IpcClient&) = delete;

    // Reads latest frame header from POSIX SHM and converts RGBA32 -> Cairo ARGB32 when frame changes.
    // `argb_dest` must have space for VIRTUAL_WIDTH * VIRTUAL_HEIGHT uint32_t pixels.
    FrameSnapshot poll_shm(uint32_t* argb_dest, int argb_stride_bytes, uint64_t last_frame_index);
    void close_shm();

    // Ensures tray-console-daemon is running and its UNIX socket is responsive.
    void ensure_daemon_async(std::function<void(bool ok)> callback = nullptr);
    bool spawn_daemon();

    // Sends a JSON command to the daemon UNIX domain socket asynchronously and parses JSON reply.
    void send_command_async(const std::string& json_payload,
                            std::function<void(const IpcResponse&)> callback = nullptr);

    void send_load_cartridge(const std::string& path,
                             std::function<void(const IpcResponse&)> callback = nullptr);
    void send_unload(std::function<void(const IpcResponse&)> callback = nullptr);
    void send_button(const std::string& name, bool pressed);
    void send_quit_and_cleanup();

    static void unlink_stale_shm();
    static void unlink_stale_socket();

private:
    bool ensure_shm_mapped(bool force_stat = false);
    static std::string find_daemon_binary();

    int m_shm_fd = -1;
    void* m_shm_ptr = nullptr;
    size_t m_shm_size = 0;
    ino_t m_shm_inode = 0;
    dev_t m_shm_dev = 0;
    int m_stat_counter = 0;

    GSocketClient* m_socket_client = nullptr;
    GSocketAddress* m_socket_addr = nullptr;
    GCancellable* m_cancellable = nullptr;
};

} // namespace trayplay::xfce
