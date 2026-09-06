#include "ipc_server.h"
#include <nlohmann/json.hpp>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>
#include <cstring>
#include <iostream>
#include <sstream>

namespace trayplay {

IpcServer::IpcServer(GameLoop& game_loop, VM& vm, Framebuffer& framebuffer,
                     const std::string& socket_path,
                     const std::string& shm_name)
    : m_game_loop(game_loop),
      m_vm(vm),
      m_framebuffer(framebuffer),
      m_socket_path(socket_path),
      m_shm_name(shm_name) {
    m_shm_size = sizeof(SharedFrameHeader) + Framebuffer::BUFFER_SIZE_BYTES;
}

IpcServer::~IpcServer() {
    stop();
}

bool IpcServer::start() {
    if (m_running.load()) return true;

    init_shm();

    // Setup UNIX domain socket
    ::unlink(m_socket_path.c_str());
    m_server_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (m_server_fd < 0) {
        std::cerr << "[IPC] Failed to create UNIX domain socket: " << strerror(errno) << std::endl;
        return false;
    }

    struct sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, m_socket_path.c_str(), sizeof(addr.sun_path) - 1);

    if (::bind(m_server_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "[IPC] Failed to bind socket to " << m_socket_path << ": " << strerror(errno) << std::endl;
        ::close(m_server_fd);
        m_server_fd = -1;
        return false;
    }

    // Set socket permissions for non-root user access
    ::chmod(m_socket_path.c_str(), 0666);

    if (::listen(m_server_fd, 5) < 0) {
        std::cerr << "[IPC] Failed to listen on socket: " << strerror(errno) << std::endl;
        ::close(m_server_fd);
        m_server_fd = -1;
        return false;
    }

    m_running.store(true);
    m_server_thread = std::thread(&IpcServer::socket_server_thread, this);

    std::cout << "[IPC] Server active: SHM='/" << m_shm_name << "', Socket='" << m_socket_path << "'" << std::endl;
    return true;
}

void IpcServer::stop() {
    m_running.store(false);

    if (m_server_thread.joinable()) {
        m_server_thread.join();
    }

    if (m_server_fd >= 0) {
        ::close(m_server_fd);
        m_server_fd = -1;
    }
    ::unlink(m_socket_path.c_str());

    cleanup_shm();
}

void IpcServer::init_shm() {
    std::string shm_key = "/" + m_shm_name;
    ::shm_unlink(shm_key.c_str());

    m_shm_fd = ::shm_open(shm_key.c_str(), O_CREAT | O_RDWR, 0666);
    if (m_shm_fd < 0) {
        std::cerr << "[IPC] Failed to open POSIX shared memory: " << strerror(errno) << std::endl;
        return;
    }

    if (::ftruncate(m_shm_fd, static_cast<off_t>(m_shm_size)) != 0) {
        std::cerr << "[IPC] Failed to resize shared memory: " << strerror(errno) << std::endl;
        return;
    }

    m_shm_ptr = ::mmap(nullptr, m_shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, m_shm_fd, 0);
    if (m_shm_ptr == MAP_FAILED) {
        std::cerr << "[IPC] Failed to mmap shared memory: " << strerror(errno) << std::endl;
        m_shm_ptr = nullptr;
        return;
    }

    // Initialize initial header
    SharedFrameHeader header;
    std::memset(&header, 0, sizeof(header));
    header.magic = 0x54524159; // 'TRAY'
    header.version = 1;
    header.width = Framebuffer::WIDTH;
    header.height = Framebuffer::HEIGHT;
    header.pitch = Framebuffer::WIDTH * sizeof(uint32_t);
    header.state = 0; // STOPPED
    header.fps = 0.0f;
    std::strncpy(header.title, "No Cartridge", sizeof(header.title) - 1);

    std::memcpy(m_shm_ptr, &header, sizeof(header));
    std::memset(static_cast<uint8_t*>(m_shm_ptr) + sizeof(header), 0, Framebuffer::BUFFER_SIZE_BYTES);
}

void IpcServer::cleanup_shm() {
    if (m_shm_ptr && m_shm_ptr != MAP_FAILED) {
        ::munmap(m_shm_ptr, m_shm_size);
        m_shm_ptr = nullptr;
    }
    if (m_shm_fd >= 0) {
        ::close(m_shm_fd);
        m_shm_fd = -1;
    }
    std::string shm_key = "/" + m_shm_name;
    ::shm_unlink(shm_key.c_str());
}

void IpcServer::set_cartridge_title(const std::string& title) {
    std::lock_guard<std::mutex> lock(m_title_mutex);
    m_current_title = title;
}

void IpcServer::publish_frame(const Framebuffer& fb, const GameTelemetry& telemetry) {
    if (!m_shm_ptr) return;

    auto* header = static_cast<SharedFrameHeader*>(m_shm_ptr);
    header->magic = 0x54524159;
    header->version = 1;
    header->width = Framebuffer::WIDTH;
    header->height = Framebuffer::HEIGHT;
    header->pitch = Framebuffer::WIDTH * sizeof(uint32_t);
    header->frame_index = ++m_frame_counter;
    header->state = static_cast<uint32_t>(m_game_loop.get_state());
    header->fps = static_cast<float>(telemetry.fps);

    {
        std::lock_guard<std::mutex> lock(m_title_mutex);
        std::strncpy(header->title, m_current_title.c_str(), sizeof(header->title) - 1);
    }

    uint8_t* pixel_dest = static_cast<uint8_t*>(m_shm_ptr) + sizeof(SharedFrameHeader);
    std::memcpy(pixel_dest, fb.data(), Framebuffer::BUFFER_SIZE_BYTES);
}

void IpcServer::socket_server_thread() {
    while (m_running.load()) {
        struct pollfd pfd;
        pfd.fd = m_server_fd;
        pfd.events = POLLIN;

        int ret = ::poll(&pfd, 1, 200); // 200ms timeout for cancellation check
        if (ret <= 0) continue;

        if (pfd.revents & POLLIN) {
            int client_fd = ::accept(m_server_fd, nullptr, nullptr);
            if (client_fd >= 0) {
                handle_client(client_fd);
                ::close(client_fd);
            }
        }
    }
}

void IpcServer::handle_client(int client_fd) {
    char buffer[4096];
    ssize_t bytes_read = ::read(client_fd, buffer, sizeof(buffer) - 1);
    if (bytes_read <= 0) return;

    buffer[bytes_read] = '\0';
    std::string request_str(buffer);

    // Process line by line
    std::istringstream stream(request_str);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty() || line == "\r") continue;
        std::string response = process_command(line) + "\n";
        ::send(client_fd, response.c_str(), response.size(), MSG_NOSIGNAL);
    }
}

std::string IpcServer::process_command(const std::string& json_str) {
    nlohmann::json response;
    try {
        auto j = nlohmann::json::parse(json_str);
        std::string cmd = j.value("cmd", "");

        if (cmd == "load") {
            std::string path = j.value("path", "");
            std::string error;
            auto cart_opt = Cartridge::load(path, error);
            if (!cart_opt) {
                response["status"] = "error";
                response["message"] = error;
            } else {
                if (m_game_loop.load_cartridge(*cart_opt)) {
                    set_cartridge_title(cart_opt->get_metadata().title);
                    response["status"] = "ok";
                    response["title"] = cart_opt->get_metadata().title;
                    response["author"] = cart_opt->get_metadata().author;
                    response["version"] = cart_opt->get_metadata().version;
                } else {
                    response["status"] = "error";
                    response["message"] = m_game_loop.get_last_error();
                }
            }
        } else if (cmd == "btn") {
            std::string name = j.value("name", "");
            bool pressed = j.value("pressed", false);
            m_vm.set_button(name, pressed);
            response["status"] = "ok";
        } else if (cmd == "unload" || cmd == "eject") {
            m_game_loop.unload_cartridge();
            set_cartridge_title("No Cartridge");
            publish_frame(m_framebuffer, m_game_loop.get_telemetry());
            response["status"] = "ok";
        } else if (cmd == "pause") {
            m_game_loop.pause();
            response["status"] = "ok";
        } else if (cmd == "resume") {
            m_game_loop.resume();
            response["status"] = "ok";
        } else if (cmd == "status") {
            response["status"] = "ok";
            response["state"] = static_cast<int>(m_game_loop.get_state());
            response["fps"] = m_game_loop.get_telemetry().fps;
            {
                std::lock_guard<std::mutex> lock(m_title_mutex);
                response["title"] = m_current_title;
            }
        } else if (cmd == "quit") {
            m_game_loop.stop();
            m_running.store(false);
            response["status"] = "ok";
        } else {
            response["status"] = "error";
            response["message"] = "Unknown command: " + cmd;
        }
    } catch (const std::exception& e) {
        response["status"] = "error";
        response["message"] = e.what();
    }

    return response.dump();
}

} // namespace trayplay
