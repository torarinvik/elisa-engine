// Streamed playback checks for probe::audio::Service. Built twice by
// scripts/run_boundary_sanitized.py: under AddressSanitizer/UBSan and under
// ThreadSanitizer, where a live null device consumes the ring on its own
// thread while this thread refills it.
#define MA_NO_FLAC
#define MA_NO_MP3
#define MA_NO_VORBIS
#define MA_NO_OPUS
#include "miniaudio_service.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

namespace {

namespace audio = probe::audio;

constexpr uint32_t RATE = 8000;
constexpr uint32_t SOURCE_FRAMES = 20000;

int failures = 0;

bool check(bool condition, const char* label) {
    if (!condition) {
        std::fprintf(stderr, "stream harness: FAILED %s\n", label);
        ++failures;
    }
    return condition;
}

int16_t source_sample(uint32_t frame) {
    return static_cast<int16_t>(static_cast<int>((frame * 3u) % 30000u) - 15000);
}

std::string write_source_wav() {
    const char* directory = std::getenv("TMPDIR");
    std::string path = std::string(directory != nullptr ? directory : "/tmp") + "/elisa-stream-harness.wav";
    const uint32_t data_bytes = SOURCE_FRAMES * 2;
    std::vector<uint8_t> wav(44 + data_bytes, 0);
    auto put = [&wav](size_t offset, uint32_t value, int bytes) {
        for (int index = 0; index < bytes; ++index) wav[offset + index] = static_cast<uint8_t>(value >> (8 * index));
    };
    std::copy_n("RIFF", 4, wav.begin());
    put(4, static_cast<uint32_t>(wav.size() - 8), 4);
    std::copy_n("WAVEfmt ", 8, wav.begin() + 8);
    put(16, 16, 4);
    put(20, 1, 2);
    put(22, 1, 2);
    put(24, RATE, 4);
    put(28, RATE * 2, 4);
    put(32, 2, 2);
    put(34, 16, 2);
    std::copy_n("data", 4, wav.begin() + 36);
    put(40, data_bytes, 4);
    for (uint32_t frame = 0; frame < SOURCE_FRAMES; ++frame) {
        put(44 + frame * 2, static_cast<uint16_t>(source_sample(frame)), 2);
    }
    FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) return {};
    const bool written = std::fwrite(wav.data(), 1, wav.size(), file) == wav.size();
    std::fclose(file);
    return written ? path : std::string();
}

// Mixes `frames` frames and checks they continue the source from `cursor`.
bool mixes_source(audio::Service& service, uint32_t& cursor, uint32_t frames) {
    std::vector<int16_t> output(frames);
    service.mix_for_test(output.data(), frames);
    for (uint32_t index = 0; index < frames; ++index) {
        if (output[index] != source_sample((cursor + index) % SOURCE_FRAMES)) return false;
    }
    cursor += frames;
    return true;
}

bool all_silent(audio::Service& service, uint32_t frames) {
    std::vector<int16_t> output(frames, 1);
    service.mix_for_test(output.data(), frames);
    for (const int16_t sample : output) if (sample != 0) return false;
    return true;
}

void deterministic_checks(const std::string& path) {
    audio::Service service;
    audio::StreamOpenStatus opened = audio::StreamOpenStatus::Opened;
    check(service.open_stream(path.c_str(), false, audio::Bus::Music, 1.0f, opened).slot == UINT32_MAX,
        "a stream needs an initialized device");
    check(service.initialize_null(RATE, 1) && service.stop_device_for_test(), "null device");
    service.open_stream("/nonexistent/elisa-stream.wav", false, audio::Bus::Music, 1.0f, opened);
    check(opened == audio::StreamOpenStatus::DecodeFailed, "a missing file is DecodeFailed");

    const audio::StreamHandle music = service.open_stream(path.c_str(), false, audio::Bus::Music, 1.0f, opened);
    check(opened == audio::StreamOpenStatus::Opened && music.slot < audio::MAX_STREAMS, "stream opens");
    uint32_t cursor = 0;
    check(mixes_source(service, cursor, 1000), "prefilled frames match the source");
    // Ring holds 4000 frames: without a refill the request starves.
    std::vector<int16_t> starved(4000);
    service.mix_for_test(starved.data(), 4000);
    cursor += 3000;
    audio::StreamStatus status = service.stream_status(music);
    check(status.state == audio::StreamState::Playing && status.frames_played == 4000 &&
        status.underrun_frames == 1000 && status.underrun_events == 1, "a starved callback is an underrun");

    check(service.set_bus_paused(audio::Bus::Music, true), "pause music bus");
    check(service.pump_streams() && all_silent(service, 500), "a paused bus is silent");
    check(service.stream_status(music).frames_played == 4000, "a paused stream does not advance");
    check(service.set_bus_paused(audio::Bus::Music, false), "resume music bus");
    while (cursor < SOURCE_FRAMES) {
        service.pump_streams();
        const uint32_t frames = std::min<uint32_t>(1000, SOURCE_FRAMES - cursor);
        if (!check(mixes_source(service, cursor, frames), "refilled frames continue the source")) break;
    }
    check(all_silent(service, 100), "a one-shot stream ends in silence");
    status = service.stream_status(music);
    check(status.state == audio::StreamState::Finished && status.frames_played == SOURCE_FRAMES &&
        status.underrun_frames == 1000, "a drained one-shot stream is Finished without new underruns");

    check(service.seek_stream(music, 0) == audio::StreamSeekStatus::Ended, "an ended one-shot cannot seek");
    check(service.stop_stream(music) && !service.stop_stream(music), "stop invalidates the handle");
    check(service.stream_status(music).state == audio::StreamState::Invalid, "stale handle has no status");
    const audio::StreamHandle looped = service.open_stream(path.c_str(), true, audio::Bus::Music, 1.0f, opened);
    check(looped.slot == music.slot && looped.generation != music.generation, "stopped slot is reused");
    const audio::StreamHandle second = service.open_stream(path.c_str(), false, audio::Bus::Music, 0.0f, opened);
    service.open_stream(path.c_str(), false, audio::Bus::Music, 1.0f, opened);
    check(second.slot < audio::MAX_STREAMS && opened == audio::StreamOpenStatus::Capacity,
        "stream slots are bounded");
    check(service.stop_stream(second), "stop the muted stream");
    cursor = 0;
    bool wrapped = true;
    while (cursor < SOURCE_FRAMES + 3000 && wrapped) {
        service.pump_streams();
        wrapped = mixes_source(service, cursor, 1000);
    }
    check(wrapped && service.stream_status(looped).underrun_frames == 0, "a looped stream wraps seamlessly");
    // Seeking drops the buffered frames and plays on from the target.
    check(service.seek_stream(looped, 12345) == audio::StreamSeekStatus::Seeked &&
        service.stream_status(looped).position == 12345, "seek reports its target position");
    cursor = 12345;
    check(mixes_source(service, cursor, 1000) && service.stream_status(looped).position == 13345,
        "a seek plays on from the target frame");
    check(service.seek_stream(looped, SOURCE_FRAMES - 500) == audio::StreamSeekStatus::Seeked, "seek near the end");
    cursor = SOURCE_FRAMES - 500;
    service.pump_streams();
    check(mixes_source(service, cursor, 1000) && service.stream_status(looped).position == 500,
        "a looped seek wraps across the end");
    check(service.seek_stream(looped, SOURCE_FRAMES) == audio::StreamSeekStatus::OutOfRange &&
        service.seek_stream(music, 0) == audio::StreamSeekStatus::Invalid, "bad seeks are rejected");
    check(service.stream_status(looped).underrun_frames == 0, "seeking causes no underrun");
    check(service.set_stream_gain(looped, 0.0f) && all_silent(service, 100), "stream gain applies");
    service.shutdown();
    check(service.active_streams() == 0 && service.stream_status(looped).state == audio::StreamState::Invalid,
        "shutdown releases every stream");
}

// A running null device consumes the ring on its own thread while this
// thread refills it, pauses and resumes, and recovers the device.
void concurrent_checks(const std::string& path) {
    audio::Service service;
    audio::StreamOpenStatus opened = audio::StreamOpenStatus::Capacity;
    check(service.initialize_null(RATE, 1), "running null device");
    const audio::StreamHandle music = service.open_stream(path.c_str(), true, audio::Bus::Music, 0.5f, opened);
    check(opened == audio::StreamOpenStatus::Opened, "looped stream opens on a running device");
    auto pump_for = [&service](int milliseconds) {
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
        while (std::chrono::steady_clock::now() < end) {
            service.pump_streams();
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
        }
    };
    pump_for(250);
    const audio::StreamStatus running = service.stream_status(music);
    check(running.frames_played > 0 && running.underrun_frames == 0, "the device consumes a refilled stream");
    service.set_bus_paused(audio::Bus::Music, true);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    const uint64_t paused_at = service.stream_status(music).frames_played;
    pump_for(100);
    check(service.stream_status(music).frames_played == paused_at, "a paused stream holds its position");
    service.set_bus_paused(audio::Bus::Music, false);
    check(service.reopen_null(), "device recovery");
    pump_for(150);
    const audio::StreamStatus recovered = service.stream_status(music);
    check(recovered.state == audio::StreamState::Playing && recovered.frames_played > paused_at &&
        recovered.underrun_frames == 0, "a stream keeps playing across device recovery");
    check(service.seek_stream(music, 100) == audio::StreamSeekStatus::Seeked, "seek while the device runs");
    pump_for(100);
    const audio::StreamStatus sought = service.stream_status(music);
    check(sought.position > 100 && sought.position < 100 + RATE && sought.underrun_frames == 0,
        "a stream plays on from a seek on a running device");
    std::fprintf(stdout, "stream harness: frames=%llu underruns=%llu contended=%llu\n",
        static_cast<unsigned long long>(recovered.frames_played),
        static_cast<unsigned long long>(recovered.underrun_frames),
        static_cast<unsigned long long>(service.contended_callbacks()));
    service.shutdown();
}

} // namespace

int main() {
    const std::string path = write_source_wav();
    if (!check(!path.empty(), "write source wav")) return 1;
    deterministic_checks(path);
    concurrent_checks(path);
    std::remove(path.c_str());
    if (failures != 0) return 1;
    std::fprintf(stdout, "stream harness passed\n");
    return 0;
}
