// Device lifecycle checks for probe::audio::Service (plan item S01). Built by
// scripts/run_boundary_sanitized.py under ASan/UBSan and ThreadSanitizer with
// live null devices: injected open failures, device loss and reopen, fallback
// when the default output fails, and shutdown must leave exactly zero or one
// open device and no data callback after close.
#define ELISA_AUDIO_TEST_PROBE 1
#define MA_NO_FLAC
#define MA_NO_MP3
#define MA_NO_VORBIS
#define MA_NO_OPUS
#include "miniaudio_service.h"

#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

namespace {

namespace audio = probe::audio;
constexpr uint32_t RATE = 8000;
int failures = 0;

bool check(bool condition, const char* label) {
    if (!condition) {
        std::fprintf(stderr, "lifecycle harness: FAILED %s\n", label);
        ++failures;
    }
    return condition;
}

std::vector<uint8_t> tone_wav(uint32_t frames) {
    std::vector<uint8_t> wav(44 + frames * 2, 0);
    auto put = [&wav](size_t at, uint32_t value, int bytes) {
        for (int i = 0; i < bytes; ++i) wav[at + i] = static_cast<uint8_t>(value >> (8 * i));
    };
    std::copy_n("RIFF", 4, wav.begin());
    put(4, static_cast<uint32_t>(wav.size() - 8), 4);
    std::copy_n("WAVEfmt ", 8, wav.begin() + 8);
    put(16, 16, 4); put(20, 1, 2); put(22, 1, 2); put(24, RATE, 4); put(28, RATE * 2, 4);
    put(32, 2, 2); put(34, 16, 2);
    std::copy_n("data", 4, wav.begin() + 36);
    put(40, frames * 2, 4);
    for (uint32_t f = 0; f < frames; ++f) put(44 + f * 2, static_cast<uint16_t>(f % 2 ? 9000 : -9000), 2);
    return wav;
}

// Waits (bounded) until the device thread has delivered more callbacks.
bool callbacks_advance(const audio::Service& service) {
    const uint64_t before = service.data_callbacks_for_test();
    for (int i = 0; i < 200; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        if (service.data_callbacks_for_test() > before) return true;
    }
    return false;
}

bool callbacks_quiet(const audio::Service& service) {
    const uint64_t before = service.data_callbacks_for_test();
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    return service.data_callbacks_for_test() == before;
}

void initialization_failure() {
    audio::Service service;
    service.fail_device_inits_for_test(1);
    check(!service.initialize_null(RATE, 1) && !service.initialized_for_test() &&
        audio::Service::open_devices_for_test() == 0, "an injected open failure leaves no device");
    check(callbacks_quiet(service) && service.data_callbacks_for_test() == 0,
        "a failed open never calls back");
    const std::vector<uint8_t> wav = tone_wav(64);
    check(service.decode_clip(wav.data(), wav.size(), RATE, 1).slot == UINT32_MAX,
        "a failed service rejects clips");
    check(service.initialize_null(RATE, 1) && audio::Service::open_devices_for_test() == 1 &&
        callbacks_advance(service), "the service opens after a failed attempt");
    check(!service.initialize_null(RATE, 1) && audio::Service::open_devices_for_test() == 1,
        "a second open is refused while one device is live");
    service.shutdown();
    check(audio::Service::open_devices_for_test() == 0 && callbacks_quiet(service),
        "shutdown closes the device and stops callbacks");
}

void loss_and_reopen() {
    audio::Service service;
    check(service.initialize_null(RATE, 1), "lifecycle service initializes");
    const std::vector<uint8_t> wav = tone_wav(RATE);
    const audio::ClipHandle clip = service.decode_clip(wav.data(), wav.size(), RATE, 1);
    const audio::VoiceHandle voice = service.play(clip, true);
    check(callbacks_advance(service), "the live device calls back");
    service.request_device_recovery_for_test();
    check(service.take_device_recovery_request() && !service.take_device_recovery_request(),
        "device loss raises one recovery request");
    check(service.reopen_null() && audio::Service::open_devices_for_test() == 1,
        "reopen after loss keeps exactly one device");
    check(!service.voice_live(voice) && service.clip_live(clip), "reopen ends voices and keeps clips");
    const audio::VoiceHandle replay = service.play(clip, true);
    check(replay.slot < audio::MAX_VOICES && callbacks_advance(service),
        "the reopened device plays and calls back");

    // Default output fails (as when it was unplugged): fall back to null.
    service.fail_device_inits_for_test(1);
    check(service.reopen_preferring_default() == 1 && audio::Service::open_devices_for_test() == 1 &&
        callbacks_advance(service), "a failed default reopen falls back to the silent route");
    // Both routes fail: the service is down with nothing open, then recovers.
    service.fail_device_inits_for_test(2);
    check(service.reopen_preferring_default() == 0 && !service.initialized_for_test() &&
        audio::Service::open_devices_for_test() == 0 && callbacks_quiet(service),
        "a failed reopen leaves no device and no callbacks");
    service.request_device_recovery_for_test();
    check(!service.take_device_recovery_request(), "a closed device ignores loss notifications");
    check(service.play(clip).slot == UINT32_MAX, "a closed service refuses playback");
    check(service.initialize_null(RATE, 1) && service.clip_live(clip) &&
        service.play(clip).slot < audio::MAX_VOICES && callbacks_advance(service),
        "the service reopens after a total failure with its clips");
    service.shutdown();
    service.request_device_recovery_for_test();
    check(!service.take_device_recovery_request() && audio::Service::open_devices_for_test() == 0 &&
        callbacks_quiet(service) && !service.clip_live(clip), "shutdown leaves no device, callback or clip");
}

void repeated_cycles() {
    audio::Service service;
    for (int cycle = 0; cycle < 20; ++cycle) {
        service.fail_device_inits_for_test(cycle % 3 == 0 ? 1 : 0);
        if (!service.initialize_null(RATE, 2)) check(service.initialize_null(RATE, 2), "cycle retry opens");
        check(audio::Service::open_devices_for_test() == 1, "one device per cycle");
        if (cycle % 2 == 0) check(service.reopen_null(), "cycle reopen");
        service.shutdown();
        check(audio::Service::open_devices_for_test() == 0, "cycle shutdown closes the device");
    }
    check(callbacks_quiet(service), "no callback after the last cycle");
}

}  // namespace

int main() {
    initialization_failure();
    loss_and_reopen();
    repeated_cycles();
    {
        audio::Service owner;
        check(owner.initialize_null(RATE, 1), "scoped service opens");
    }
    check(audio::Service::open_devices_for_test() == 0, "destroying a service closes its device");
    if (failures != 0) return 1;
    std::printf("lifecycle harness: injected failures, loss/reopen, fallback and shutdown leave <=1 device "
        "and no callbacks after close\n");
    return 0;
}
