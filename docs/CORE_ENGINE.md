# Core Engine Architecture & Technical Specification

This document provides a comprehensive technical breakdown of the C++ Core subsystem (`tray-console-daemon`) for **TrayPlay**, detailing its headless daemon design, memory layout, deterministic game loop, Lua virtual machine sandbox, and zero-copy IPC streaming architecture.

---

## 1. Architectural Philosophy

TrayPlay is architected around the **Headless Core + Thin Native Client** pattern. Rather than bundling monolithic frameworks (such as Electron or heavy game engines), the computing engine is decoupled from the user interface:

- **The Core Daemon (`tray-console-daemon`)**: A pure C++17 background process responsible for parsing cartridges, executing sandboxed game code, calculating physics, maintaining a fixed 60 FPS loop, and rendering pixels into a shared memory buffer.
- **The Native Frontends (Clients)**: Lightweight, platform-native overlays (GNOME Shell GJS extension, macOS Swift/SwiftUI menu bar agent, Windows C++/WinRT tray icon) that display the framebuffer and capture user input.

This separation guarantees:
1. **100% Cross-Platform Code Reuse**: Physics, scripting, cartridge loading, and rendering logic are written once in C++ and compiled identically across Linux, macOS, and Windows.
2. **Deterministic Behavior**: Game timing and logic run independently of host display refresh rates (60 Hz, 144 Hz, 240 Hz).
3. **Ultra-Low Resource Footprint**: The entire daemon operates with a virtual memory footprint of ~6 MB and less than 3% CPU usage.

---

## 2. Subsystem Structure & Responsibilities

The core subsystem files reside in `core/`:

```text
core/
├── framebuffer.h / .cpp    # Virtual 320x180 RGBA32 video memory and 2D drawing primitives
├── vm.h / .cpp             # Sandboxed Lua 5.5 VM with Sol2 C++ bindings and fantasy console API
├── cartridge.h / .cpp      # Cartridge package loader (.gpak ZIP via miniz, raw .lua, directories)
├── game_loop.h / .cpp      # Deterministic 60 FPS fixed-timestep loop, state machine, and telemetry
├── ipc_server.h / .cpp     # Zero-copy POSIX Shared Memory publisher and Unix Domain Socket server
└── main.cpp                # Process entry point, signal management, and lifecycle orchestrator
```

---

## 3. Key Components & Implementation Details

### 3.1. Virtual Framebuffer (`Framebuffer`)

Instead of requiring complex hardware rendering pipelines (DirectX, Metal, Vulkan), TrayPlay uses **Software Rendering** directly in system RAM:
- **Resolution**: Fixed widescreen **320 × 180 pixels** (16:9 aspect ratio).
- **Pixel Format**: 32-bit RGBA (8 bits per channel, packed in little-endian order: `0xAABBGGRR`).
- **Memory Footprint**: $320 \times 180 \times 4\text{ bytes} = 230{,}400\text{ bytes}$ (~225 KB).
- **Drawing Primitives**:
  - `clear(color)`: Fast memory fill of the framebuffer.
  - `draw_line(x0, y0, x1, y1, color)`: Integer Bresenham line algorithm.
  - `draw_rect(...)` / `fill_rect(...)`: Clamped scanline fill with boundary clipping.
  - `draw_circle(...)` / `fill_circle(...)`: Midpoint circle algorithm with symmetric scanline rasterization.

### 3.2. Scripting Virtual Machine & Sandbox (`VM`)

The game scripting environment is powered by **Lua 5.5** integrated via the header-only **Sol2** library:

#### Security & Sandboxing
Untrusted cartridges downloaded from the web must not compromise the host system. The VM opens only safe standard libraries (`base`, `math`, `string`, `table`). Potentially destructive libraries—such as `io`, `os`, and `package`—are deliberately omitted, preventing cartridges from accessing the filesystem or executing shell commands.

#### Microconsole API
The VM exposes classic microconsole functions with floating-point coordinate tolerance:
- **Graphics**: `cls(col)`, `pset(x, y, col)`, `pget(x, y)`, `line(x0, y0, x1, y1, col)`, `rect(x, y, w, h, col)`, `rectfill(x, y, w, h, col)`, `circ(x, y, r, col)`, `circfill(x, y, r, col)`.
- **Palette**: Built-in 16-color fantasy console palette (indices `0`–`15`), with seamless fallback to custom 32-bit `rgba(r, g, b, a)` colors.
- **Input**: `btn(name)` queries directional buttons (`up`, `down`, `left`, `right`, `a`, `b`, `start`, `select`).
- **Lifecycle Hooks**: Cartridges implement standard callbacks:
  - `init()`: Called once upon cartridge load.
  - `update(dt)`: Called 60 times per second for game logic.
  - `draw()`: Called every frame to render graphics into the framebuffer.

### 3.3. Cartridge Package Format (`Cartridge`)

A `.gpak` file is an industry-standard ZIP archive containing the game assets and manifest:

```text
game.gpak (ZIP container):
├── game.json     # Metadata manifest (title, author, version, entry point)
└── main.lua      # Lua game entry point
```

- **Decompression via `miniz`**: In-memory unzipping eliminates disk writes and temporary directory bloat.
- **Developer Mode**: In addition to packaged `.gpak` archives, the loader directly executes raw `.lua` scripts and uncompressed directories for instant iteration without re-packaging.

### 3.4. Deterministic Game Loop (`GameLoop`)

Timing precision is critical for retro games. TrayPlay implements the classic **Fixed Timestep Accumulator** pattern:

$$\Delta t = \frac{1}{60} \approx 0.016666\text{ seconds}$$

```cpp
auto now = std::chrono::steady_clock::now();
double frame_time = std::chrono::duration<double>(now - m_last_time).count();
m_last_time = now;

if (frame_time > 0.25) frame_time = 0.25; // Spiral of death clamp

m_accumulator += frame_time;
while (m_accumulator >= TARGET_DT) {
    m_vm.update(TARGET_DT);
    m_accumulator -= TARGET_DT;
}
m_vm.draw();
```

#### State Machine
The game loop operates as a finite state machine:
- `STOPPED` (0): No cartridge loaded; daemon sleeps in an idle state (0% CPU).
- `RUNNING` (1): Cartridge active; running fixed-timestep updates and rendering at 60 FPS.
- `PAUSED` (2): Game logic and frame updates frozen.
- `CRASHED` (3): Script exception captured; error message recorded without terminating the daemon.

---

## 4. Zero-Copy IPC Architecture

To stream 60 FPS uncompressed video to native desktop frontends without IPC bottlenecks, TrayPlay implements a hybrid communication channel:

```
┌────────────────────────────────────────────────────────┐
│                  C++ Core Daemon                       │
│                                                        │
│  ┌──────────────┐          ┌────────────────────────┐  │
│  │ GameLoop     │          │ Unix Domain Socket     │  │
│  │ (60 FPS)     │          │ Server (/tmp/*.sock)   │  │
│  └──────┬───────┘          └───────────▲────────────┘  │
│         │ (Render)                     │ (JSON commands)
└─────────┼──────────────────────────────┼───────────────┘
          │ (Zero-Copy memcpy)           │
          ▼                              ▼
┌─────────────────────────────┐   ┌──────────────────────┐
│ POSIX Shared Memory Segment │   │ Unix Socket Client   │
│ (/dev/shm/trayplay_*)       │   │                      │
│ [Header] + [320x180x4 RGBA] │   │                      │
└─────────┬───────────────────┘   └───────────▲──────────┘
          │ (GLib.MappedFile)                 │
┌─────────▼───────────────────────────────────┴──────────┐
│             Native Desktop Frontend                    │
│             (GNOME Shell / macOS / Windows)            │
└────────────────────────────────────────────────────────┘
```

### 4.1. Video Stream: POSIX Shared Memory

The daemon creates a shared memory segment at `/dev/shm/trayplay_framebuffer` formatted as:

```cpp
#pragma pack(push, 1)
struct SharedFrameHeader {
    uint32_t magic;         // 0x54524159 ('TRAY')
    uint32_t version;       // Protocol version (1)
    uint32_t width;         // 320
    uint32_t height;        // 180
    uint32_t pitch;         // 1280 (320 * 4 bytes)
    uint64_t frame_index;   // Monotonically increasing frame counter
    uint32_t state;         // 0: STOPPED, 1: RUNNING, 2: PAUSED, 3: CRASHED
    float fps;              // Smoothed real-time FPS
    char title[64];         // Null-terminated cartridge title
};
#pragma pack(pop)
```

Directly following the 96-byte header is the raw 230,400-byte RGBA32 pixel buffer. Frontend clients map this file once using memory-mapped I/O (`GLib.MappedFile` in GJS, `mmap` / POSIX API in Swift, `MapViewOfFile` in Windows), eliminating all IPC serialization and network copying overhead.

### 4.2. Command Stream: Unix Domain Socket

A stream socket at `/tmp/trayplay.sock` accepts newline-delimited JSON commands:

| Command | Payload Example | Response | Description |
|---|---|---|---|
| `load` | `{"cmd":"load","path":"games/pong.gpak"}` | `{"status":"ok","title":"Neon Pong"}` | Loads and starts a cartridge |
| `unload` | `{"cmd":"unload"}` | `{"status":"ok"}` | Unloads cartridge, resets to WAITING |
| `btn` | `{"cmd":"btn","name":"up","pressed":true}` | `{"status":"ok"}` | Updates virtual controller state |
| `pause` | `{"cmd":"pause"}` | `{"status":"ok"}` | Pauses execution |
| `resume` | `{"cmd":"resume"}` | `{"status":"ok"}` | Resumes execution |
| `status` | `{"cmd":"status"}` | `{"status":"ok","state":1,"fps":60.0}` | Queries daemon telemetry |
| `quit` | `{"cmd":"quit"}` | `{"status":"ok"}` | Gracefully shuts down the daemon |

---

## 5. Engineering Robustness & Signal Handling

The daemon is engineered as an enterprise-grade background system daemon:
1. **`SIGPIPE` Protection**: Network writes to prematurely closed client connections use `MSG_NOSIGNAL` and ignore `SIGPIPE` to prevent sudden daemon crashes.
2. **Orderly Descriptor Teardown**: Server threads are explicitly joined before socket file descriptors are closed, preventing race conditions with `poll()` / `select()`.
3. **Graceful Shutdown**: Unlinks socket files and POSIX shared memory segments on exit (`SIGINT`, `SIGTERM`, `SIGHUP`), ensuring zero orphan artifacts left on the host system.
