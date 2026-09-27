#pragma once

// Bounded streamed playback for long sources such as music. The owner thread
// opens a miniaudio decoder and refills a fixed ring buffer; the device
// callback only copies frames out of the ring. The ring is allocated once
// when the device is first initialized and reused, so neither side allocates
// while a stream plays and the callback never performs file IO.
#include "miniaudio.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

namespace probe::audio {

constexpr uint32_t MAX_STREAMS = 2;
// Refilled once per rendered frame, half a second covers long frame stalls.
constexpr uint32_t STREAM_BUFFER_MILLISECONDS = 500;

struct StreamHandle {
    uint32_t slot = UINT32_MAX;
    uint32_t generation = 0;
};

enum class StreamState : uint8_t { Invalid = 0, Playing = 1, Finished = 2 };

struct StreamStatus {
    StreamState state = StreamState::Invalid;
    uint64_t frames_played = 0;
    uint64_t underrun_frames = 0;
    uint32_t underrun_events = 0;
};

// One producer (owner thread) and one consumer (device callback). `written`
// and `consumed` are monotonically increasing frame counters; the ring index
// is the counter modulo capacity.
struct Stream {
    // Owner-thread state.
    ma_decoder decoder{};
    bool decoder_open = false;
    bool looped = false;
    bool reserved = false;
    uint32_t generation = 0;
    // Written by the owner before `live` is published, read by the callback.
    std::vector<int16_t> ring;
    uint32_t capacity_frames = 0;
    uint8_t bus = 0;
    float gain = 1.0f;
    bool live = false; // guarded by the service mutex
    std::atomic<uint64_t> written{0};
    std::atomic<uint64_t> consumed{0};
    std::atomic<bool> source_done{false};
    std::atomic<bool> drained{false};
    std::atomic<uint64_t> underrun_frames{0};
    std::atomic<uint32_t> underrun_events{0};

    // Keeps an allocation that already has the right size, so a device
    // reopen at the same format leaves playing streams untouched.
    void allocate(uint32_t sample_rate, uint32_t channels) {
        const uint32_t frames = std::max<uint32_t>(1, sample_rate * STREAM_BUFFER_MILLISECONDS / 1000);
        if (capacity_frames == frames && ring.size() == static_cast<size_t>(frames) * channels) return;
        ring.assign(static_cast<size_t>(frames) * channels, 0);
        capacity_frames = frames;
    }

    void release_storage() {
        std::vector<int16_t>().swap(ring);
        capacity_frames = 0;
    }

    void reset_counters() {
        written.store(0, std::memory_order_relaxed);
        consumed.store(0, std::memory_order_relaxed);
        source_done.store(false, std::memory_order_relaxed);
        drained.store(false, std::memory_order_relaxed);
        underrun_frames.store(0, std::memory_order_relaxed);
        underrun_events.store(0, std::memory_order_relaxed);
    }

    void close_decoder() {
        if (decoder_open) ma_decoder_uninit(&decoder);
        decoder_open = false;
    }

    // Owner thread: decode into free ring space. Returns false only when the
    // decoder fails; reaching the end of a one-shot source is not a failure.
    bool refill(uint32_t channels) {
        if (!decoder_open || capacity_frames == 0) return true;
        uint64_t write = written.load(std::memory_order_relaxed);
        uint64_t space = capacity_frames - (write - consumed.load(std::memory_order_acquire));
        bool rewound_without_data = false;
        while (space > 0) {
            const uint32_t offset = static_cast<uint32_t>(write % capacity_frames);
            const uint64_t chunk = std::min<uint64_t>(space, capacity_frames - offset);
            ma_uint64 read = 0;
            const ma_result status = ma_decoder_read_pcm_frames(
                &decoder, ring.data() + static_cast<size_t>(offset) * channels, chunk, &read);
            write += read;
            space -= read;
            written.store(write, std::memory_order_release);
            if (read > 0) rewound_without_data = false;
            if (read == chunk) continue;
            if (status != MA_SUCCESS && status != MA_AT_END) {
                close_decoder();
                source_done.store(true, std::memory_order_release);
                return false;
            }
            // A looped source restarts; an empty looped source must not spin.
            if (looped && !rewound_without_data &&
                ma_decoder_seek_to_pcm_frame(&decoder, 0) == MA_SUCCESS) {
                rewound_without_data = true;
                continue;
            }
            close_decoder();
            source_done.store(true, std::memory_order_release);
            break;
        }
        return true;
    }

    // Device callback, under the service mutex: add up to `frames` frames to
    // `output` without allocating, and record a starved request as an underrun.
    void mix_into(int16_t* output, uint32_t frames, uint32_t channels, float mixed_gain) {
        const uint64_t read = consumed.load(std::memory_order_relaxed);
        const uint64_t available = written.load(std::memory_order_acquire) - read;
        const uint32_t count = static_cast<uint32_t>(std::min<uint64_t>(frames, available));
        for (uint32_t frame = 0; frame < count; ++frame) {
            const size_t base = static_cast<size_t>((read + frame) % capacity_frames) * channels;
            for (uint32_t channel = 0; channel < channels; ++channel) {
                const int mixed = output[frame * channels + channel] +
                    static_cast<int>(static_cast<float>(ring[base + channel]) * mixed_gain);
                output[frame * channels + channel] = static_cast<int16_t>(std::clamp(mixed, -32768, 32767));
            }
        }
        consumed.store(read + count, std::memory_order_release);
        if (count == frames) return;
        if (source_done.load(std::memory_order_acquire)) {
            drained.store(true, std::memory_order_release);
            return;
        }
        underrun_frames.fetch_add(frames - count, std::memory_order_relaxed);
        underrun_events.fetch_add(1, std::memory_order_relaxed);
    }

    StreamStatus status() const {
        StreamStatus result;
        result.state = drained.load(std::memory_order_acquire) ? StreamState::Finished : StreamState::Playing;
        result.frames_played = consumed.load(std::memory_order_acquire);
        result.underrun_frames = underrun_frames.load(std::memory_order_relaxed);
        result.underrun_events = underrun_events.load(std::memory_order_relaxed);
        return result;
    }
};

enum class StreamOpenStatus : uint8_t { Opened, Capacity, DecodeFailed };

// Fixed stream slots with generation-checked handles. Owner-thread methods
// take the service mutex only to publish or retire a slot; decoding happens
// outside it. `mix` runs in the device callback with that mutex held.
class StreamTable {
public:
    void allocate(uint32_t sample_rate, uint32_t channels) {
        for (Stream& stream : streams_) stream.allocate(sample_rate, channels);
    }

    StreamHandle open(const char* path, bool looped, uint8_t bus, float gain,
        uint32_t sample_rate, uint32_t channels, std::mutex& mutex, StreamOpenStatus& result) {
        result = StreamOpenStatus::Capacity;
        uint32_t slot = MAX_STREAMS;
        for (uint32_t index = 0; index < MAX_STREAMS; ++index) {
            if (!streams_[index].reserved && streams_[index].capacity_frames > 0) { slot = index; break; }
        }
        if (slot == MAX_STREAMS || streams_[slot].generation == UINT32_MAX) return {};
        Stream& stream = streams_[slot];
        result = StreamOpenStatus::DecodeFailed;
        const ma_decoder_config config = ma_decoder_config_init(ma_format_s16, channels, sample_rate);
        if (ma_decoder_init_file(path, &config, &stream.decoder) != MA_SUCCESS) return {};
        stream.decoder_open = true;
        stream.looped = looped;
        stream.reset_counters();
        if (!stream.refill(channels) || stream.written.load(std::memory_order_relaxed) == 0) {
            stream.close_decoder();
            return {};
        }
        {
            std::lock_guard<std::mutex> guard(mutex);
            stream.bus = bus;
            stream.gain = gain;
            stream.generation += 1;
            stream.live = true;
        }
        stream.reserved = true;
        result = StreamOpenStatus::Opened;
        return StreamHandle{slot, stream.generation};
    }

    // Retiring a slot under the mutex guarantees the callback no longer reads
    // its ring, so the decoder can close and the slot be reused immediately.
    bool stop(StreamHandle handle, std::mutex& mutex) {
        if (!valid(handle)) return false;
        Stream& stream = streams_[handle.slot];
        {
            std::lock_guard<std::mutex> guard(mutex);
            stream.live = false;
        }
        stream.close_decoder();
        stream.reserved = false;
        return true;
    }

    bool set_gain(StreamHandle handle, float gain, std::mutex& mutex) {
        if (!valid(handle)) return false;
        std::lock_guard<std::mutex> guard(mutex);
        streams_[handle.slot].gain = gain;
        return true;
    }

    // Owner thread, once per frame. Returns false when a decoder failed; that
    // stream then finishes with the audio it already buffered.
    bool pump(uint32_t channels) {
        bool healthy = true;
        for (Stream& stream : streams_) {
            if (stream.reserved) healthy = stream.refill(channels) && healthy;
        }
        return healthy;
    }

    StreamStatus status(StreamHandle handle) const {
        if (!valid(handle)) return {};
        return streams_[handle.slot].status();
    }

    uint32_t live_count() const {
        uint32_t count = 0;
        for (const Stream& stream : streams_) count += stream.reserved ? 1 : 0;
        return count;
    }

    // Call only while no device callback can run.
    void shutdown(std::mutex& mutex) {
        for (Stream& stream : streams_) {
            {
                std::lock_guard<std::mutex> guard(mutex);
                stream.live = false;
            }
            stream.close_decoder();
            stream.reserved = false;
            stream.release_storage();
        }
    }

    template <size_t BusCount>
    void mix(int16_t* output, uint32_t frames, uint32_t channels,
        const std::array<float, BusCount>& bus_gains, const std::array<bool, BusCount>& bus_paused) {
        for (Stream& stream : streams_) {
            if (!stream.live || stream.bus >= BusCount || bus_paused[stream.bus]) continue;
            stream.mix_into(output, frames, channels, stream.gain * bus_gains[stream.bus]);
        }
    }

private:
    bool valid(StreamHandle handle) const {
        return handle.slot < MAX_STREAMS && streams_[handle.slot].reserved &&
            streams_[handle.slot].generation == handle.generation;
    }

    std::array<Stream, MAX_STREAMS> streams_{};
};

} // namespace probe::audio
