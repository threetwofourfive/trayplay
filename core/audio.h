#pragma once

#include <cstdint>
#include <string>
#include <array>
#include <vector>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>

#if __has_include(<SDL2/SDL.h>)
#include <SDL2/SDL.h>
#else
#include <SDL.h>
#endif

namespace trayplay {

enum class Waveform : uint8_t {
    PULSE = 0,    // Square / pulse wave (classic 8-bit lead)
    TRIANGLE = 1, // Soft triangle wave (smooth retro bass)
    NOISE = 2,    // White / LFSR noise (explosions, percussions, hits)
    SINE = 3,     // Pure sinusoidal tone
    SAWTOOTH = 4  // Sharp rich sawtooth wave
};

struct AudioChannel {
    bool active{false};
    Waveform wave{Waveform::PULSE};
    float freq{440.0f};
    float sweep{0.0f};      // Pitch bend in Hz per second
    float volume{0.5f};     // 0.0 to 1.0
    float phase{0.0f};      // 0.0 to 1.0
    int remaining_samples{0};
    int total_samples{0};
    uint32_t noise_seed{0xACE1u}; // LFSR seed for retro noise
};

class Audio {
public:
    static constexpr int SAMPLE_RATE = 44100;
    static constexpr int BUFFER_SAMPLES = 1024;
    static constexpr size_t NUM_CHANNELS = 4; // 4 polyphonic chiptune channels

    Audio();
    ~Audio();

    // Prevent copying
    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;

    bool init();
    void shutdown();
    bool is_initialized() const noexcept { return m_initialized; }

    // Play a single tone / note on an automatic or specific channel
    void beep(float freq, float duration_ms, float volume = 0.5f, Waveform wave = Waveform::PULSE);
    void tone(int channel, float freq, float duration_ms, float volume = 0.5f, Waveform wave = Waveform::PULSE, float sweep = 0.0f);

    // Play preset retro sound effects
    void play_sfx(const std::string& name);

    // Silence audio
    void stop();
    void stop_channel(int channel);

private:
    static void audio_callback(void* userdata, Uint8* stream, int len);
    void generate_samples(int16_t* stream, int num_samples);
    int find_free_channel() const;
    void stream_worker();

    SDL_AudioDeviceID m_device{0};
    bool m_initialized{false};
    std::array<AudioChannel, NUM_CHANNELS> m_channels;

    // Desktop streaming backend fallback (PipeWire / ALSA)
    FILE* m_pipe{nullptr};
    std::thread m_pipe_thread;
    std::atomic<bool> m_pipe_running{false};
    mutable std::mutex m_pipe_mutex;

    // SFX sequence helper for multi-step sounds (like two-tone coin/score)
    struct SfxStep {
        float delay_ms{0.0f};
        int channel{3};
        float freq{440.0f};
        float duration_ms{100.0f};
        float volume{0.5f};
        Waveform wave{Waveform::PULSE};
        float sweep{0.0f};
    };
    std::vector<SfxStep> m_pending_sfx;
    float m_sfx_timer_ms{0.0f};
};

} // namespace trayplay
