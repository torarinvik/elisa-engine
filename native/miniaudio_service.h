#pragma once

// One-owner miniaudio playback service. Clip decoding happens before a clip is
// published; the device callback only mixes fixed voice slots and allocates
// nothing. Gameplay owns when to play or stop a voice.
#include "miniaudio.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace probe::audio {

constexpr uint32_t MAX_CLIPS = 16;
constexpr uint32_t MAX_VOICES = 32;

enum class Bus : uint8_t { Music = 0, Sfx = 1, Ui = 2, Count = 3 };
constexpr uint32_t BUS_COUNT = static_cast<uint32_t>(Bus::Count);

struct ClipHandle {
    uint32_t slot = UINT32_MAX;
    uint32_t generation = 0;
};

struct VoiceHandle {
    uint32_t slot = UINT32_MAX;
    uint32_t generation = 0;
};

struct ListenerState {
    float position[3] = {};
    float velocity[3] = {};
};

class Service {
public:
    Service() = default;
    Service(const Service&) = delete;
    Service& operator=(const Service&) = delete;
    ~Service() { shutdown(); }

    bool initialize_null(uint32_t sample_rate = 8000, uint32_t channels = 1) {
        if (initialized_ || sample_rate == 0 || channels == 0 || channels > 2) return false;
        ma_backend backends[] = {ma_backend_null};
        const ma_context_config context_config = ma_context_config_init();
        if (ma_context_init(backends, 1, &context_config, &context_) != MA_SUCCESS) return false;
        ma_device_config config = ma_device_config_init(ma_device_type_playback);
        config.playback.format = ma_format_s16;
        config.playback.channels = channels;
        config.sampleRate = sample_rate;
        config.dataCallback = &Service::data_callback;
        config.pUserData = this;
        if (ma_device_init(&context_, &config, &device_) != MA_SUCCESS) {
            ma_context_uninit(&context_);
            return false;
        }
        sample_rate_ = sample_rate;
        channels_ = channels;
        initialized_ = true;
        if (ma_device_start(&device_) != MA_SUCCESS) {
            ma_device_uninit(&device_);
            ma_context_uninit(&context_);
            initialized_ = false;
            sample_rate_ = 0;
            channels_ = 0;
            return false;
        }
        return true;
    }

    // Reopen only the device/context while retaining decoded clips. Voices
    // are stopped before the callback is detached, so no callback can observe
    // a half-reopened device and callers must explicitly replay them.
    bool reopen_null() {
        if (!initialized_) return false;
        const uint32_t rate = sample_rate_;
        const uint32_t channels = channels_;
        for (Voice& voice : voices_) voice.live = false;
        ma_device_uninit(&device_);
        ma_context_uninit(&context_);
        initialized_ = false;
        return initialize_null(rate, channels);
    }

    void shutdown() {
        if (initialized_) {
            ma_device_uninit(&device_);
            ma_context_uninit(&context_);
        }
        initialized_ = false;
        sample_rate_ = 0;
        channels_ = 0;
        for (Clip& clip : clips_) clip = {};
        for (Voice& voice : voices_) voice = {};
    }

    ClipHandle decode_clip(const uint8_t* bytes, size_t size, uint32_t rate, uint32_t channels) {
        if (!initialized_ || bytes == nullptr || size == 0 || rate == 0 || channels == 0 || channels > 2) return {};
        uint32_t slot = MAX_CLIPS;
        for (uint32_t index = 0; index < MAX_CLIPS; ++index) {
            if (!clips_[index].live) { slot = index; break; }
        }
        if (slot == MAX_CLIPS) return {};
        ma_decoder_config config = ma_decoder_config_init(ma_format_s16, channels, rate);
        ma_decoder decoder;
        if (ma_decoder_init_memory(bytes, size, &config, &decoder) != MA_SUCCESS) return {};
        ma_uint64 frames = 0;
        const ma_result length_status = ma_decoder_get_length_in_pcm_frames(&decoder, &frames);
        if (length_status != MA_SUCCESS || frames == 0 || frames > 4 * 1024 * 1024) {
            ma_decoder_uninit(&decoder);
            return {};
        }
        Clip& clip = clips_[slot];
        clip.samples.resize(static_cast<size_t>(frames) * channels);
        ma_uint64 read = 0;
        const ma_result read_status = ma_decoder_read_pcm_frames(&decoder, clip.samples.data(), frames, &read);
        ma_decoder_uninit(&decoder);
        if (read_status != MA_SUCCESS || read != frames) {
            clip = {};
            return {};
        }
        clip.rate = rate;
        clip.channels = channels;
        clip.generation = clip.generation == UINT32_MAX ? 0 : clip.generation + 1;
        if (clip.generation == 0) { clip = {}; return {}; }
        clip.live = true;
        return ClipHandle{slot, clip.generation};
    }

    VoiceHandle play(ClipHandle clip, bool looped = false, Bus bus = Bus::Sfx,
        float gain = 1.0f, uint32_t priority = 0) {
        if (!clip_valid(clip)) return {};
        if (!bus_valid(bus) || gain < 0.0f || gain > 4.0f) return {};
        uint32_t slot = MAX_VOICES;
        if (active_bus_voices(bus) >= bus_budgets_[bus_index(bus)]) {
            uint32_t victim = MAX_VOICES;
            for (uint32_t index = 0; index < MAX_VOICES; ++index) {
                const Voice& voice = voices_[index];
                if (voice.live && voice.bus == bus_index(bus) &&
                    voice.priority < priority &&
                    (victim == MAX_VOICES || voice.priority < voices_[victim].priority)) {
                    victim = index;
                }
            }
            if (victim < MAX_VOICES) {
                voices_[victim].live = false;
                slot = victim;
            }
        } else {
            for (uint32_t index = 0; index < MAX_VOICES; ++index) {
                if (!voices_[index].live) { slot = index; break; }
            }
        }
        if (slot == MAX_VOICES) return {};
        Voice& voice = voices_[slot];
        voice.clip = clip.slot;
        voice.cursor = 0;
        voice.looped = looped;
        voice.bus = bus_index(bus);
        voice.gain = gain;
        voice.priority = priority;
        voice.generation = voice.generation == UINT32_MAX ? 0 : voice.generation + 1;
        if (voice.generation == 0) { voice = {}; return {}; }
        voice.live = true;
        return VoiceHandle{slot, voice.generation};
    }

    bool set_bus_gain(Bus bus, float gain) {
        if (!bus_valid(bus) || gain < 0.0f || gain > 4.0f) return false;
        bus_gains_[bus_index(bus)] = gain;
        return true;
    }

    float bus_gain(Bus bus) const {
        return bus_valid(bus) ? bus_gains_[bus_index(bus)] : 0.0f;
    }

    bool set_voice_budget(Bus bus, uint32_t budget) {
        if (!bus_valid(bus) || budget > MAX_VOICES) return false;
        bus_budgets_[bus_index(bus)] = budget;
        return true;
    }

    void set_listener(ListenerState state) { listener_ = state; }

    ListenerState listener() const { return listener_; }

    bool stop(VoiceHandle handle) {
        if (handle.slot >= MAX_VOICES || !voices_[handle.slot].live ||
            voices_[handle.slot].generation != handle.generation) return false;
        voices_[handle.slot].live = false;
        return true;
    }

    bool set_voice_position(VoiceHandle handle, float x, float y, float z) {
        if (!voice_live(handle)) return false;
        Voice& voice = voices_[handle.slot];
        voice.position[0] = x;
        voice.position[1] = y;
        voice.position[2] = z;
        voice.spatialized = true;
        return true;
    }

    bool set_voice_range(VoiceHandle handle, float minimum, float maximum) {
        if (!voice_live(handle) || minimum < 0.0f || maximum <= minimum) return false;
        Voice& voice = voices_[handle.slot];
        voice.minimum_distance = minimum;
        voice.maximum_distance = maximum;
        return true;
    }

    bool set_voice_velocity(VoiceHandle handle, float x, float y, float z) {
        if (!voice_live(handle)) return false;
        Voice& voice = voices_[handle.slot];
        voice.velocity[0] = x;
        voice.velocity[1] = y;
        voice.velocity[2] = z;
        return true;
    }

    bool set_voice_occlusion(VoiceHandle handle, float occlusion) {
        if (!voice_live(handle) || occlusion < 0.0f || occlusion > 1.0f) return false;
        voices_[handle.slot].occlusion = occlusion;
        return true;
    }

    float voice_doppler_ratio(VoiceHandle handle) const {
        if (!voice_live(handle)) return 1.0f;
        const Voice& voice = voices_[handle.slot];
        const float dx = voice.position[0] - listener_.position[0];
        const float dy = voice.position[1] - listener_.position[1];
        const float dz = voice.position[2] - listener_.position[2];
        const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (distance <= 0.0001f) return 1.0f;
        const float relative_speed = ((voice.velocity[0] - listener_.velocity[0]) * dx +
            (voice.velocity[1] - listener_.velocity[1]) * dy +
            (voice.velocity[2] - listener_.velocity[2]) * dz) / distance;
        const float denominator = 343.0f + relative_speed;
        if (denominator <= 171.5f) return 2.0f;
        return std::clamp(343.0f / denominator, 0.5f, 2.0f);
    }

    bool voice_live(VoiceHandle handle) const {
        return handle.slot < MAX_VOICES && voices_[handle.slot].live &&
            voices_[handle.slot].generation == handle.generation;
    }

    uint32_t active_voices() const {
        uint32_t count = 0;
        for (const Voice& voice : voices_) count += voice.live ? 1 : 0;
        return count;
    }

    void mix_for_test(int16_t* output, uint32_t frames) {
        if (output == nullptr) return;
        mix(output, frames);
    }

private:
    struct Clip {
        std::vector<int16_t> samples;
        uint32_t generation = 0;
        uint32_t rate = 0;
        uint32_t channels = 0;
        bool live = false;
    };

    struct Voice {
        uint32_t clip = UINT32_MAX;
        uint32_t generation = 0;
        size_t cursor = 0;
        bool looped = false;
        uint8_t bus = static_cast<uint8_t>(Bus::Sfx);
        float gain = 1.0f;
        uint32_t priority = 0;
        float position[3] = {};
        float velocity[3] = {};
        float minimum_distance = 1.0f;
        float maximum_distance = 32.0f;
        bool spatialized = false;
        float occlusion = 0.0f;
        bool live = false;
    };

    static constexpr uint32_t bus_index(Bus bus) {
        return static_cast<uint32_t>(bus);
    }

    static constexpr bool bus_valid(Bus bus) {
        return bus_index(bus) < bus_index(Bus::Count);
    }

    uint32_t active_bus_voices(Bus bus) const {
        const uint8_t index = static_cast<uint8_t>(bus_index(bus));
        uint32_t count = 0;
        for (const Voice& voice : voices_) count += voice.live && voice.bus == index ? 1 : 0;
        return count;
    }

    bool clip_valid(ClipHandle handle) const {
        return handle.slot < MAX_CLIPS && clips_[handle.slot].live &&
            clips_[handle.slot].generation == handle.generation;
    }

    void mix(int16_t* output, uint32_t frames) {
        std::fill(output, output + frames * channels_, 0);
        for (Voice& voice : voices_) {
            if (!voice.live || voice.clip >= MAX_CLIPS || !clips_[voice.clip].live) continue;
            Clip& clip = clips_[voice.clip];
            float spatial_gain = 1.0f;
            if (voice.spatialized) {
                const float dx = voice.position[0] - listener_.position[0];
                const float dy = voice.position[1] - listener_.position[1];
                const float dz = voice.position[2] - listener_.position[2];
                const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
                if (distance >= voice.maximum_distance) spatial_gain = 0.0f;
                else if (distance > voice.minimum_distance) {
                    spatial_gain = (voice.maximum_distance - distance) /
                        (voice.maximum_distance - voice.minimum_distance);
                }
            }
            const float gain = voice.gain * bus_gains_[voice.bus] * spatial_gain * (1.0f - voice.occlusion);
            for (uint32_t frame = 0; frame < frames; ++frame) {
                if (voice.cursor >= clip.samples.size() / clip.channels) {
                    if (!voice.looped) { voice.live = false; break; }
                    voice.cursor = 0;
                }
                for (uint32_t channel = 0; channel < channels_; ++channel) {
                    const uint32_t source_channel = std::min(channel, clip.channels - 1);
                    const int mixed = output[frame * channels_ + channel] + static_cast<int>(
                        clip.samples[voice.cursor * clip.channels + source_channel] * gain);
                    output[frame * channels_ + channel] = static_cast<int16_t>(
                        std::clamp(mixed, -32768, 32767));
                }
                ++voice.cursor;
            }
        }
    }

    static void data_callback(ma_device* device, void* output, const void*, ma_uint32 frames) {
        auto* service = static_cast<Service*>(device->pUserData);
        if (service != nullptr) service->mix(static_cast<int16_t*>(output), frames);
    }

    ma_context context_{};
    ma_device device_{};
    std::array<Clip, MAX_CLIPS> clips_{};
    std::array<Voice, MAX_VOICES> voices_{};
    uint32_t sample_rate_ = 0;
    uint32_t channels_ = 0;
    bool initialized_ = false;
    ListenerState listener_{};
    std::array<float, BUS_COUNT> bus_gains_{{1.0f, 1.0f, 1.0f}};
    std::array<uint32_t, BUS_COUNT> bus_budgets_{{MAX_VOICES, MAX_VOICES, MAX_VOICES}};
};

} // namespace probe::audio
