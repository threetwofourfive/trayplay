#include "audio.h"
#include <iostream>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <chrono>

namespace trayplay {

Audio::Audio() = default;

Audio::~Audio() {
    shutdown();
}

bool Audio::init() {
    if (m_initialized) return true;

    // 1. Try standard SDL2 audio device first (works when SDL2 has ALSA/Pulse/CoreAudio/WASAPI drivers)
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) == 0) {
        SDL_AudioSpec desired;
        SDL_AudioSpec obtained;
        SDL_zero(desired);
        desired.freq = SAMPLE_RATE;
        desired.format = AUDIO_S16SYS;
        desired.channels = 1;
        desired.samples = BUFFER_SAMPLES;
        desired.callback = audio_callback;
        desired.userdata = this;

        m_device = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
        if (m_device != 0) {
            SDL_PauseAudioDevice(m_device, 0);
            m_initialized = true;
            std::cout << "[Audio] SDL2 audio device initialized (44100 Hz, 16-bit Mono)" << std::endl;
            return true;
        }
    }

    // 2. Fall back to native Linux desktop streaming (PipeWire pw-cat or ALSA aplay)
    FILE* pipe = ::popen("pw-cat -p --raw --rate 44100 --channels 1 --format s16 --latency 20ms - 2>/dev/null", "w");
    if (!pipe) {
        pipe = ::popen("aplay -q -t raw -f S16_LE -r 44100 -c 1 2>/dev/null", "w");
    }

    if (pipe) {
        m_pipe = pipe;
        m_pipe_running.store(true);
        m_pipe_thread = std::thread(&Audio::stream_worker, this);
        m_initialized = true;
        std::cout << "[Audio] Linux desktop streaming backend initialized (PipeWire/ALSA low-latency pipe)" << std::endl;
        return true;
    }

    std::cerr << "[Audio] Warning: No audio device available (daemon will run in silent mode)" << std::endl;
    return false;
}

void Audio::shutdown() {
    if (m_pipe_running.load()) {
        m_pipe_running.store(false);
        if (m_pipe_thread.joinable()) {
            m_pipe_thread.join();
        }
    }
    if (m_pipe) {
        ::pclose(m_pipe);
        m_pipe = nullptr;
    }

    if (m_device != 0) {
        SDL_CloseAudioDevice(m_device);
        m_device = 0;
    }
    if (m_initialized) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        m_initialized = false;
    }
}

void Audio::audio_callback(void* userdata, Uint8* stream, int len) {
    auto* self = static_cast<Audio*>(userdata);
    int num_samples = len / static_cast<int>(sizeof(int16_t));
    self->generate_samples(reinterpret_cast<int16_t*>(stream), num_samples);
}

void Audio::stream_worker() {
    constexpr int CHUNK_SAMPLES = 882; // 20ms of audio at 44100 Hz
    int16_t buffer[CHUNK_SAMPLES];

    while (m_pipe_running.load()) {
        auto start = std::chrono::steady_clock::now();
        {
            std::lock_guard<std::mutex> lock(m_pipe_mutex);
            generate_samples(buffer, CHUNK_SAMPLES);
        }

        if (m_pipe) {
            size_t written = ::fwrite(buffer, sizeof(int16_t), CHUNK_SAMPLES, m_pipe);
            if (written < CHUNK_SAMPLES) {
                break;
            }
            ::fflush(m_pipe);
        }

        auto end = std::chrono::steady_clock::now();
        double elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();
        double sleep_ms = 20.0 - elapsed_ms;
        if (sleep_ms > 1.0) {
            std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(sleep_ms));
        }
    }
}

void Audio::generate_samples(int16_t* stream, int num_samples) {
    float dt_sec = 1.0f / static_cast<float>(SAMPLE_RATE);
    float dt_ms = dt_sec * 1000.0f;

    for (int s = 0; s < num_samples; ++s) {
        // 1. Process multi-step SFX queue
        if (!m_pending_sfx.empty()) {
            m_sfx_timer_ms += dt_ms;
            for (auto it = m_pending_sfx.begin(); it != m_pending_sfx.end();) {
                if (m_sfx_timer_ms >= it->delay_ms) {
                    int ch = it->channel;
                    if (ch >= 0 && ch < static_cast<int>(NUM_CHANNELS)) {
                        m_channels[ch].active = true;
                        m_channels[ch].wave = it->wave;
                        m_channels[ch].freq = it->freq;
                        m_channels[ch].sweep = it->sweep;
                        m_channels[ch].volume = it->volume;
                        m_channels[ch].phase = 0.0f;
                        int total = static_cast<int>((it->duration_ms / 1000.0f) * SAMPLE_RATE);
                        m_channels[ch].total_samples = total;
                        m_channels[ch].remaining_samples = total;
                    }
                    it = m_pending_sfx.erase(it);
                } else {
                    ++it;
                }
            }
            if (m_pending_sfx.empty()) {
                m_sfx_timer_ms = 0.0f;
            }
        }

        // 2. Synthesize audio across all 4 channels
        float mixed = 0.0f;

        for (size_t c = 0; c < NUM_CHANNELS; ++c) {
            auto& ch = m_channels[c];
            if (!ch.active || ch.remaining_samples <= 0) {
                ch.active = false;
                continue;
            }

            float sample = 0.0f;
            switch (ch.wave) {
                case Waveform::PULSE:
                    sample = (ch.phase < 0.5f) ? 1.0f : -1.0f;
                    break;
                case Waveform::TRIANGLE:
                    sample = 2.0f * std::fabs(2.0f * (ch.phase - std::floor(ch.phase + 0.5f))) - 1.0f;
                    break;
                case Waveform::NOISE: {
                    uint32_t bit = ((ch.noise_seed >> 0) ^ (ch.noise_seed >> 1)) & 1;
                    ch.noise_seed = (ch.noise_seed >> 1) | (bit << 15);
                    sample = bit ? 1.0f : -1.0f;
                    break;
                }
                case Waveform::SINE:
                    sample = std::sin(ch.phase * 6.283185307179586f);
                    break;
                case Waveform::SAWTOOTH:
                    sample = 2.0f * (ch.phase - std::floor(ch.phase + 0.5f));
                    break;
            }

            // Envelope anti-click: brief attack and decay
            float env = 1.0f;
            int fade_samples = std::min(441, ch.total_samples / 4); // max ~10ms fade
            if (ch.remaining_samples < fade_samples && fade_samples > 0) {
                env = static_cast<float>(ch.remaining_samples) / static_cast<float>(fade_samples);
            }
            int attack_samples = std::min(88, ch.total_samples / 10); // max ~2ms attack
            int played = ch.total_samples - ch.remaining_samples;
            if (played < attack_samples && attack_samples > 0) {
                env *= static_cast<float>(played) / static_cast<float>(attack_samples);
            }

            mixed += sample * ch.volume * env;

            // Advance phase & apply pitch bend
            ch.phase += ch.freq * dt_sec;
            if (ch.phase >= 1.0f) {
                ch.phase -= std::floor(ch.phase);
            }
            ch.freq += ch.sweep * dt_sec;
            if (ch.freq < 10.0f) ch.freq = 10.0f;

            ch.remaining_samples--;
        }

        // Clamp mixed audio
        mixed = std::clamp(mixed, -1.0f, 1.0f);
        stream[s] = static_cast<int16_t>(mixed * 28000.0f);
    }
}

int Audio::find_free_channel() const {
    for (size_t i = 0; i < NUM_CHANNELS; ++i) {
        if (!m_channels[i].active) return static_cast<int>(i);
    }
    int min_idx = 0;
    int min_rem = m_channels[0].remaining_samples;
    for (size_t i = 1; i < NUM_CHANNELS; ++i) {
        if (m_channels[i].remaining_samples < min_rem) {
            min_rem = m_channels[i].remaining_samples;
            min_idx = static_cast<int>(i);
        }
    }
    return min_idx;
}

void Audio::beep(float freq, float duration_ms, float volume, Waveform wave) {
    if (!m_initialized) return;

    auto action = [this, freq, duration_ms, volume, wave]() {
        int ch = find_free_channel();
        auto& c = m_channels[ch];
        c.active = true;
        c.wave = wave;
        c.freq = freq;
        c.sweep = 0.0f;
        c.volume = std::clamp(volume, 0.0f, 1.0f);
        c.phase = 0.0f;
        int total = static_cast<int>((duration_ms / 1000.0f) * SAMPLE_RATE);
        c.total_samples = total;
        c.remaining_samples = total;
    };

    if (m_device != 0) {
        SDL_LockAudioDevice(m_device);
        action();
        SDL_UnlockAudioDevice(m_device);
    } else {
        std::lock_guard<std::mutex> lock(m_pipe_mutex);
        action();
    }
}

void Audio::tone(int channel, float freq, float duration_ms, float volume, Waveform wave, float sweep) {
    if (!m_initialized) return;
    if (channel < 0 || channel >= static_cast<int>(NUM_CHANNELS)) return;

    auto action = [this, channel, freq, duration_ms, volume, wave, sweep]() {
        auto& ch = m_channels[channel];
        ch.active = true;
        ch.wave = wave;
        ch.freq = freq;
        ch.sweep = sweep;
        ch.volume = std::clamp(volume, 0.0f, 1.0f);
        ch.phase = 0.0f;
        int total = static_cast<int>((duration_ms / 1000.0f) * SAMPLE_RATE);
        ch.total_samples = total;
        ch.remaining_samples = total;
    };

    if (m_device != 0) {
        SDL_LockAudioDevice(m_device);
        action();
        SDL_UnlockAudioDevice(m_device);
    } else {
        std::lock_guard<std::mutex> lock(m_pipe_mutex);
        action();
    }
}

void Audio::play_sfx(const std::string& name) {
    if (!m_initialized) return;

    auto action = [this, &name]() {
        if (name == "hit" || name == "bounce") {
            // Short punchy bounce sound (paddle hit)
            auto& ch = m_channels[3];
            ch.active = true;
            ch.wave = Waveform::PULSE;
            ch.freq = 320.0f;
            ch.sweep = -1200.0f;
            ch.volume = 0.7f;
            ch.phase = 0.0f;
            int total = static_cast<int>((0.060f) * SAMPLE_RATE); // 60ms
            ch.total_samples = total;
            ch.remaining_samples = total;
        } else if (name == "wall") {
            // Lower frequency thump (boundary hit)
            auto& ch = m_channels[3];
            ch.active = true;
            ch.wave = Waveform::TRIANGLE;
            ch.freq = 160.0f;
            ch.sweep = -400.0f;
            ch.volume = 0.6f;
            ch.phase = 0.0f;
            int total = static_cast<int>((0.045f) * SAMPLE_RATE); // 45ms
            ch.total_samples = total;
            ch.remaining_samples = total;
        } else if (name == "score" || name == "coin") {
            // Two-tone rising chime
            m_pending_sfx.clear();
            m_sfx_timer_ms = 0.0f;

            // Note 1: E5 (659 Hz)
            auto& ch = m_channels[2];
            ch.active = true;
            ch.wave = Waveform::PULSE;
            ch.freq = 659.25f;
            ch.sweep = 0.0f;
            ch.volume = 0.7f;
            ch.phase = 0.0f;
            int total = static_cast<int>((0.080f) * SAMPLE_RATE); // 80ms
            ch.total_samples = total;
            ch.remaining_samples = total;

            // Note 2: A5 (880 Hz) delayed by 80ms
            SfxStep step2;
            step2.delay_ms = 80.0f;
            step2.channel = 2;
            step2.freq = 880.0f;
            step2.duration_ms = 160.0f;
            step2.volume = 0.7f;
            step2.wave = Waveform::PULSE;
            step2.sweep = 0.0f;
            m_pending_sfx.push_back(step2);
        } else if (name == "miss" || name == "fail") {
            auto& ch = m_channels[3];
            ch.active = true;
            ch.wave = Waveform::SAWTOOTH;
            ch.freq = 280.0f;
            ch.sweep = -700.0f;
            ch.volume = 0.65f;
            ch.phase = 0.0f;
            int total = static_cast<int>((0.250f) * SAMPLE_RATE); // 250ms
            ch.total_samples = total;
            ch.remaining_samples = total;
        } else if (name == "explosion") {
            auto& ch = m_channels[3];
            ch.active = true;
            ch.wave = Waveform::NOISE;
            ch.freq = 120.0f;
            ch.sweep = -200.0f;
            ch.volume = 0.75f;
            ch.phase = 0.0f;
            int total = static_cast<int>((0.300f) * SAMPLE_RATE); // 300ms
            ch.total_samples = total;
            ch.remaining_samples = total;
        } else if (name == "blip") {
            auto& ch = m_channels[3];
            ch.active = true;
            ch.wave = Waveform::PULSE;
            ch.freq = 900.0f;
            ch.sweep = 0.0f;
            ch.volume = 0.5f;
            ch.phase = 0.0f;
            int total = static_cast<int>((0.025f) * SAMPLE_RATE); // 25ms
            ch.total_samples = total;
            ch.remaining_samples = total;
        }
    };

    if (m_device != 0) {
        SDL_LockAudioDevice(m_device);
        action();
        SDL_UnlockAudioDevice(m_device);
    } else {
        std::lock_guard<std::mutex> lock(m_pipe_mutex);
        action();
    }
}

void Audio::stop() {
    if (!m_initialized) return;

    auto action = [this]() {
        for (auto& ch : m_channels) {
            ch.active = false;
        }
        m_pending_sfx.clear();
        m_sfx_timer_ms = 0.0f;
    };

    if (m_device != 0) {
        SDL_LockAudioDevice(m_device);
        action();
        SDL_UnlockAudioDevice(m_device);
    } else {
        std::lock_guard<std::mutex> lock(m_pipe_mutex);
        action();
    }
}

void Audio::stop_channel(int channel) {
    if (!m_initialized) return;
    if (channel < 0 || channel >= static_cast<int>(NUM_CHANNELS)) return;

    auto action = [this, channel]() {
        m_channels[channel].active = false;
    };

    if (m_device != 0) {
        SDL_LockAudioDevice(m_device);
        action();
        SDL_UnlockAudioDevice(m_device);
    } else {
        std::lock_guard<std::mutex> lock(m_pipe_mutex);
        action();
    }
}

} // namespace trayplay
