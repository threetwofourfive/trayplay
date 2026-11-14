#include "ipc_client.h"

#include <gio/gio.h>
#include <gio/gunixsocketaddress.h>
#include <glib.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#if defined(__SSE2__)
#include <emmintrin.h>
#endif
#if defined(__SSSE3__)
#include <tmmintrin.h>
#endif

namespace trayplay::xfce {

namespace {

std::string executable_directory() {
    char buf[4096];
    ssize_t len = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len <= 0) {
        return {};
    }
    buf[len] = '\0';
    gchar* dir = g_path_get_dirname(buf);
    std::string result = dir ? dir : "";
    g_free(dir);
    return result;
}

inline uint32_t rgba_pixel_to_cairo_argb32(uint32_t rgba) noexcept {
    // Source little-endian uint32: 0xAABBGGRR -> Cairo ARGB32 little-endian: 0xAARRGGBB (premultiplied)
    const uint32_t a = rgba >> 24;
    if (a == 255u) {
        return (rgba & 0xFF00FF00u) | ((rgba & 0x00FF0000u) >> 16) | ((rgba & 0x000000FFu) << 16);
    }
    if (a == 0u) {
        return 0u;
    }
    const uint32_t r = ((rgba & 0xFFu) * a + 127u) / 255u;
    const uint32_t g = (((rgba >> 8) & 0xFFu) * a + 127u) / 255u;
    const uint32_t b = (((rgba >> 16) & 0xFFu) * a + 127u) / 255u;
    return (a << 24) | (r << 16) | (g << 8) | b;
}

void rgba_to_cairo_argb32(const uint8_t* __restrict src,
                          uint32_t* __restrict dst,
                          int dst_stride_pixels,
                          int width,
                          int height) {
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
#if defined(__SSE2__)
    const __m128i alpha_mask = _mm_set1_epi32(static_cast<int>(0xFF000000u));
#if defined(__SSSE3__)
    const __m128i shuffle_rb = _mm_setr_epi8(2, 1, 0, 3, 6, 5, 4, 7, 10, 9, 8, 11, 14, 13, 12, 15);
#else
    const __m128i ga_mask = _mm_set1_epi32(static_cast<int>(0xFF00FF00u));
    const __m128i r_mask = _mm_set1_epi32(0x00FF0000);
    const __m128i b_mask = _mm_set1_epi32(0x000000FF);
#endif
#endif

    for (int y = 0; y < height; ++y) {
        uint32_t* __restrict row = dst + static_cast<size_t>(y) * dst_stride_pixels;
        const uint8_t* __restrict row_src = src + static_cast<size_t>(y) * width * 4;
        int x = 0;

#if defined(__SSE2__)
        for (; x + 4 <= width; x += 4) {
            const __m128i px = _mm_loadu_si128(reinterpret_cast<const __m128i*>(row_src + static_cast<size_t>(x) * 4));
            const __m128i cmp = _mm_cmpeq_epi32(_mm_and_si128(px, alpha_mask), alpha_mask);
            if (_mm_movemask_epi8(cmp) == 0xFFFF) {
#if defined(__SSSE3__)
                const __m128i swapped = _mm_shuffle_epi8(px, shuffle_rb);
#else
                const __m128i ga = _mm_and_si128(px, ga_mask);
                const __m128i r = _mm_and_si128(_mm_slli_epi32(px, 16), r_mask);
                const __m128i b = _mm_and_si128(_mm_srli_epi32(px, 16), b_mask);
                const __m128i swapped = _mm_or_si128(ga, _mm_or_si128(r, b));
#endif
                _mm_storeu_si128(reinterpret_cast<__m128i*>(row + x), swapped);
            } else {
                uint32_t tmp[4];
                std::memcpy(tmp, row_src + static_cast<size_t>(x) * 4, sizeof(tmp));
                row[x + 0] = rgba_pixel_to_cairo_argb32(tmp[0]);
                row[x + 1] = rgba_pixel_to_cairo_argb32(tmp[1]);
                row[x + 2] = rgba_pixel_to_cairo_argb32(tmp[2]);
                row[x + 3] = rgba_pixel_to_cairo_argb32(tmp[3]);
            }
        }
#endif

        for (; x < width; ++x) {
            uint32_t rgba = 0;
            std::memcpy(&rgba, row_src + static_cast<size_t>(x) * 4, sizeof(rgba));
            row[x] = rgba_pixel_to_cairo_argb32(rgba);
        }
    }
#else
    for (int y = 0; y < height; ++y) {
        uint32_t* row = dst + static_cast<size_t>(y) * dst_stride_pixels;
        const uint8_t* s = src + static_cast<size_t>(y) * width * 4;
        for (int x = 0; x < width; ++x) {
            uint32_t r = s[0];
            uint32_t g = s[1];
            uint32_t b = s[2];
            uint32_t a = s[3];
            if (a == 255) {
                row[x] = (0xFFu << 24) | (r << 16) | (g << 8) | b;
            } else if (a == 0) {
                row[x] = 0;
            } else {
                r = (r * a + 127u) / 255u;
                g = (g * a + 127u) / 255u;
                b = (b * a + 127u) / 255u;
                row[x] = (a << 24) | (r << 16) | (g << 8) | b;
            }
            s += 4;
        }
    }
#endif
}

struct AsyncCmdContext {
    std::string payload;
    std::function<void(const IpcResponse&)> callback;
    GCancellable* cancellable = nullptr;
    GSocketConnection* conn = nullptr;
    GBytes* payload_bytes = nullptr;
    GDataInputStream* in_stream = nullptr;
};

void free_async_cmd_context(AsyncCmdContext* ctx) {
    if (!ctx) return;
    if (ctx->in_stream) g_object_unref(ctx->in_stream);
    if (ctx->payload_bytes) g_bytes_unref(ctx->payload_bytes);
    if (ctx->conn) {
        g_io_stream_close(G_IO_STREAM(ctx->conn), nullptr, nullptr);
        g_object_unref(ctx->conn);
    }
    if (ctx->cancellable) g_object_unref(ctx->cancellable);
    delete ctx;
}

void on_read_line_done(GObject* source, GAsyncResult* res, gpointer user_data) {
    auto* ctx = static_cast<AsyncCmdContext*>(user_data);
    IpcResponse resp;
    GError* err = nullptr;
    gsize len = 0;
    char* line = g_data_input_stream_read_line_finish_utf8(G_DATA_INPUT_STREAM(source), res, &len, &err);

    if (line) {
        resp.transport_ok = true;
        try {
            auto j = nlohmann::json::parse(line);
            const std::string status = j.value("status", "");
            resp.ok = (status == "ok");
            resp.message = j.value("message", "");
            resp.title = j.value("title", "");
        } catch (...) {
            resp.ok = false;
            resp.message = "Malformed daemon response";
        }
        g_free(line);
    } else if (err) {
        const bool cancelled = g_error_matches(err, G_IO_ERROR, G_IO_ERROR_CANCELLED);
        g_error_free(err);
        if (cancelled) {
            free_async_cmd_context(ctx);
            return;
        }
    }

    if (ctx->callback) {
        ctx->callback(resp);
    }
    free_async_cmd_context(ctx);
}

void on_write_done(GObject* source, GAsyncResult* res, gpointer user_data) {
    auto* ctx = static_cast<AsyncCmdContext*>(user_data);
    GError* err = nullptr;
    gsize written = 0;
    gboolean ok = g_output_stream_write_all_finish(G_OUTPUT_STREAM(source), res, &written, &err);
    if (!ok) {
        const bool cancelled = err && g_error_matches(err, G_IO_ERROR, G_IO_ERROR_CANCELLED);
        if (err) g_error_free(err);
        if (!cancelled && ctx->callback) {
            IpcResponse resp;
            resp.transport_ok = false;
            resp.ok = false;
            resp.message = "Failed to send IPC command";
            ctx->callback(resp);
        }
        free_async_cmd_context(ctx);
        return;
    }

    if (!ctx->callback) {
        free_async_cmd_context(ctx);
        return;
    }

    GInputStream* raw_in = g_io_stream_get_input_stream(G_IO_STREAM(ctx->conn));
    ctx->in_stream = g_data_input_stream_new(raw_in);
    g_data_input_stream_read_line_async(
        ctx->in_stream,
        G_PRIORITY_DEFAULT,
        ctx->cancellable,
        on_read_line_done,
        ctx
    );
}

void on_connect_done(GObject* source, GAsyncResult* res, gpointer user_data) {
    auto* ctx = static_cast<AsyncCmdContext*>(user_data);
    GError* err = nullptr;
    ctx->conn = g_socket_client_connect_finish(G_SOCKET_CLIENT(source), res, &err);
    if (!ctx->conn) {
        const bool cancelled = err && g_error_matches(err, G_IO_ERROR, G_IO_ERROR_CANCELLED);
        if (err) g_error_free(err);
        if (!cancelled && ctx->callback) {
            IpcResponse resp;
            resp.transport_ok = false;
            resp.ok = false;
            resp.message = "Daemon socket unreachable";
            ctx->callback(resp);
        }
        free_async_cmd_context(ctx);
        return;
    }

    GOutputStream* out = g_io_stream_get_output_stream(G_IO_STREAM(ctx->conn));
    ctx->payload_bytes = g_bytes_new(ctx->payload.data(), ctx->payload.size());
    g_output_stream_write_all_async(
        out,
        g_bytes_get_data(ctx->payload_bytes, nullptr),
        g_bytes_get_size(ctx->payload_bytes),
        G_PRIORITY_DEFAULT,
        ctx->cancellable,
        on_write_done,
        ctx
    );
}

} // namespace

IpcClient::IpcClient()
    : m_socket_client(g_socket_client_new()),
      m_socket_addr(g_unix_socket_address_new(SOCKET_PATH)),
      m_cancellable(g_cancellable_new()) {
    g_socket_client_set_timeout(m_socket_client, 2);
}

IpcClient::~IpcClient() {
    if (m_cancellable) {
        g_cancellable_cancel(m_cancellable);
        g_clear_object(&m_cancellable);
    }
    g_clear_object(&m_socket_addr);
    g_clear_object(&m_socket_client);
    close_shm();
}

void IpcClient::close_shm() {
    if (m_shm_ptr && m_shm_ptr != MAP_FAILED) {
        ::munmap(m_shm_ptr, m_shm_size);
    }
    m_shm_ptr = nullptr;
    m_shm_size = 0;

    if (m_shm_fd >= 0) {
        ::close(m_shm_fd);
        m_shm_fd = -1;
    }
    m_shm_inode = 0;
    m_shm_dev = 0;
}

bool IpcClient::ensure_shm_mapped(bool force_stat) {
    // When SHM is already mapped and frames are streaming smoothly, avoid calling stat() every 16ms
    if (m_shm_ptr && !force_stat && (++m_stat_counter % 30) != 0) {
        return true;
    }

    struct stat st {};
    if (::stat(SHM_PATH, &st) != 0) {
        close_shm();
        return false;
    }

    const size_t min_size = sizeof(SharedFrameHeader) + PIXEL_BUFFER_SIZE;
    if (static_cast<size_t>(st.st_size) < min_size) {
        close_shm();
        return false;
    }

    if (m_shm_ptr && st.st_ino == m_shm_inode && st.st_dev == m_shm_dev &&
        static_cast<size_t>(st.st_size) == m_shm_size) {
        return true;
    }

    close_shm();

    int fd = ::open(SHM_PATH, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }

    struct stat fd_st {};
    if (::fstat(fd, &fd_st) != 0 || static_cast<size_t>(fd_st.st_size) < min_size) {
        ::close(fd);
        return false;
    }

    void* ptr = ::mmap(nullptr, static_cast<size_t>(fd_st.st_size), PROT_READ, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) {
        ::close(fd);
        return false;
    }

    m_shm_fd = fd;
    m_shm_ptr = ptr;
    m_shm_size = static_cast<size_t>(fd_st.st_size);
    m_shm_inode = fd_st.st_ino;
    m_shm_dev = fd_st.st_dev;
    m_stat_counter = 0;
    return true;
}

FrameSnapshot IpcClient::poll_shm(uint32_t* argb_dest, int argb_stride_bytes, uint64_t last_frame_index) {
    FrameSnapshot snap;
    if (!ensure_shm_mapped(last_frame_index == UINT64_MAX)) {
        return snap;
    }

    SharedFrameHeader header {};
    std::memcpy(&header, m_shm_ptr, sizeof(SharedFrameHeader));
    std::atomic_thread_fence(std::memory_order_acquire);

    if (header.magic != SHM_MAGIC || header.version != SHM_VERSION ||
        header.width != VIRTUAL_WIDTH || header.height != VIRTUAL_HEIGHT) {
        return snap;
    }

    snap.shm_available = true;
    snap.frame_index = header.frame_index;
    snap.state = static_cast<DaemonState>(header.state);
    snap.fps = header.fps;

    const size_t title_len = ::strnlen(header.title, sizeof(header.title));
    snap.title.assign(header.title, title_len);

    if (argb_dest && snap.state == DaemonState::RUNNING && header.frame_index != last_frame_index) {
        const uint8_t* pixels = static_cast<const uint8_t*>(m_shm_ptr) + sizeof(SharedFrameHeader);
        for (int attempt = 0; attempt < 2; ++attempt) {
            const uint64_t index_before = header.frame_index;
            rgba_to_cairo_argb32(pixels, argb_dest, argb_stride_bytes / static_cast<int>(sizeof(uint32_t)),
                                 VIRTUAL_WIDTH, VIRTUAL_HEIGHT);
            std::atomic_thread_fence(std::memory_order_acquire);

            SharedFrameHeader after {};
            std::memcpy(&after, m_shm_ptr, sizeof(SharedFrameHeader));
            if (after.frame_index == index_before || after.state != static_cast<uint32_t>(DaemonState::RUNNING)) {
                break;
            }
            header = after;
            snap.frame_index = header.frame_index;
            snap.fps = header.fps;
        }
        snap.new_frame = true;
    }

    return snap;
}

std::string IpcClient::find_daemon_binary() {
    if (const char* env_bin = g_getenv("TRAYPLAY_DAEMON")) {
        if (g_file_test(env_bin, G_FILE_TEST_IS_EXECUTABLE)) {
            return env_bin;
        }
    }

    const std::string exe_dir = executable_directory();
    if (!exe_dir.empty()) {
        const std::string local_bin = exe_dir + "/tray-console-daemon";
        if (g_file_test(local_bin.c_str(), G_FILE_TEST_IS_EXECUTABLE)) {
            return local_bin;
        }
    }

    if (gchar* in_path = g_find_program_in_path("tray-console-daemon")) {
        std::string found = in_path;
        g_free(in_path);
        return found;
    }

    const char* candidates[] = {
        "/usr/bin/tray-console-daemon",
        "/usr/local/bin/tray-console-daemon",
    };
    for (const char* candidate : candidates) {
        if (g_file_test(candidate, G_FILE_TEST_IS_EXECUTABLE)) {
            return candidate;
        }
    }

    return {};
}

bool IpcClient::spawn_daemon() {
    const std::string daemon_bin = find_daemon_binary();
    if (daemon_bin.empty()) {
        return false;
    }

    const gchar* argv[] = {daemon_bin.c_str(), nullptr};
    GError* err = nullptr;
    gboolean launched = g_spawn_async(
        nullptr,
        const_cast<gchar**>(argv),
        nullptr,
        G_SPAWN_SEARCH_PATH,
        nullptr,
        nullptr,
        nullptr,
        &err
    );
    if (err) {
        g_error_free(err);
    }
    return launched == TRUE;
}

void IpcClient::ensure_daemon_async(std::function<void(bool)> callback) {
    if (!g_file_test(SOCKET_PATH, G_FILE_TEST_EXISTS)) {
        const bool ok = spawn_daemon();
        if (callback) callback(ok);
        return;
    }

    // Verify socket responsiveness so stale sockets from SIGKILLed daemons are cleaned up
    send_command_async("{\"cmd\":\"status\"}\n", [this, callback = std::move(callback)](const IpcResponse& resp) {
        if (resp.transport_ok) {
            if (callback) callback(true);
            return;
        }
        unlink_stale_socket();
        const bool ok = spawn_daemon();
        if (callback) callback(ok);
    });
}

void IpcClient::send_command_async(const std::string& json_payload,
                                   std::function<void(const IpcResponse&)> callback) {
    if (!m_socket_client || !m_socket_addr || !g_file_test(SOCKET_PATH, G_FILE_TEST_EXISTS)) {
        if (callback) {
            IpcResponse resp;
            resp.transport_ok = false;
            resp.ok = false;
            resp.message = "Daemon socket not found";
            callback(resp);
        }
        return;
    }

    auto* ctx = new AsyncCmdContext();
    ctx->payload = json_payload;
    if (ctx->payload.empty() || ctx->payload.back() != '\n') {
        ctx->payload.push_back('\n');
    }
    ctx->callback = std::move(callback);
    ctx->cancellable = m_cancellable ? static_cast<GCancellable*>(g_object_ref(m_cancellable)) : nullptr;

    g_socket_client_connect_async(
        m_socket_client,
        G_SOCKET_CONNECTABLE(m_socket_addr),
        ctx->cancellable,
        on_connect_done,
        ctx
    );
}

void IpcClient::send_load_cartridge(const std::string& path,
                                    std::function<void(const IpcResponse&)> callback) {
    nlohmann::json j = {
        {"cmd", "load"},
        {"path", path},
    };
    send_command_async(j.dump() + "\n", std::move(callback));
}

void IpcClient::send_unload(std::function<void(const IpcResponse&)> callback) {
    send_command_async("{\"cmd\":\"unload\"}\n", std::move(callback));
}

void IpcClient::send_button(const std::string& name, bool pressed) {
    // Fast zero-JSON-DOM formatting for hot input path
    char buf[96];
    const int n = std::snprintf(
        buf,
        sizeof(buf),
        "{\"cmd\":\"btn\",\"name\":\"%s\",\"pressed\":%s}\n",
        name.c_str(),
        pressed ? "true" : "false"
    );
    if (n > 0 && static_cast<size_t>(n) < sizeof(buf)) {
        send_command_async(std::string(buf, static_cast<size_t>(n)));
        return;
    }

    nlohmann::json j = {
        {"cmd", "btn"},
        {"name", name},
        {"pressed", pressed},
    };
    send_command_async(j.dump() + "\n");
}

void IpcClient::send_quit_and_cleanup() {
    // Best-effort synchronous socket quit so daemon exits cleanly before app terminates
    if (g_file_test(SOCKET_PATH, G_FILE_TEST_EXISTS)) {
        GSocketClient* client = g_socket_client_new();
        GSocketAddress* addr = g_unix_socket_address_new(SOCKET_PATH);
        g_socket_client_set_timeout(client, 1);
        GSocketConnection* conn = g_socket_client_connect(client, G_SOCKET_CONNECTABLE(addr), nullptr, nullptr);
        if (conn) {
            GOutputStream* out = g_io_stream_get_output_stream(G_IO_STREAM(conn));
            const char msg[] = "{\"cmd\":\"quit\"}\n";
            g_output_stream_write_all(out, msg, sizeof(msg) - 1, nullptr, nullptr, nullptr);
            g_io_stream_close(G_IO_STREAM(conn), nullptr, nullptr);
            g_object_unref(conn);
        }
        g_object_unref(addr);
        g_object_unref(client);
    }

    // Fallback: if a daemon PID file still corresponds to a live tray-console-daemon process, terminate it
    std::ifstream pid_in(PID_PATH);
    pid_t pid = 0;
    if (pid_in >> pid && pid > 1) {
        if (::kill(pid, 0) == 0) {
            ::kill(pid, SIGTERM);
        }
    }

    close_shm();
}

void IpcClient::unlink_stale_shm() {
    ::unlink(SHM_PATH);
}

void IpcClient::unlink_stale_socket() {
    ::unlink(SOCKET_PATH);
}

} // namespace trayplay::xfce
