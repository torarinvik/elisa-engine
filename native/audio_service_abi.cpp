#include "audio_service_abi.h"

#include "application_abi.h"

#include "miniaudio_service.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace {

struct AudioService {
    probe::audio::Service service;
    bool initialized = false;
#if defined(ELISA_AUDIO_TEST_PROBE)
    bool fail_next_initialize_after_open = false;
#endif
};

AudioService& audio_service() {
    static AudioService service;
    return service;
}

int32_t require_application_owner() {
    const int32_t status = elisa_application_v1_validate_owner_thread();
    if (status == ELISA_APPLICATION_OK) return ELISA_AUDIO_OK;
    if (status == ELISA_APPLICATION_WRONG_THREAD) return ELISA_AUDIO_WRONG_THREAD;
    return ELISA_AUDIO_INVALID_STATE;
}

// `ELISA_AUDIO_FORCE_DEVICE_UNAVAILABLE=1` makes the default output device
// fail to open so a packaged launch can prove its silent fallback route
// without unplugging hardware; sandboxes cannot make CoreAudio fail.
bool default_device_forced_unavailable() {
    const char* value = std::getenv("ELISA_AUDIO_FORCE_DEVICE_UNAVAILABLE");
    return value != nullptr && value[0] != '\0' && value[0] != '0';
}

int32_t initialize_audio(uint32_t sample_rate, uint32_t channels, bool silent) {
    const int32_t owner_status = require_application_owner();
    if (owner_status != ELISA_AUDIO_OK) return owner_status;
    if (sample_rate < ELISA_AUDIO_MIN_SAMPLE_RATE_HZ ||
        sample_rate > ELISA_AUDIO_MAX_SAMPLE_RATE_HZ ||
        channels < ELISA_AUDIO_MIN_CHANNEL_COUNT || channels > ELISA_AUDIO_MAX_CHANNEL_COUNT) {
        return ELISA_AUDIO_INVALID_ARGUMENT;
    }
    AudioService& state = audio_service();
    if (state.initialized) return ELISA_AUDIO_INVALID_STATE;
    if (!silent && default_device_forced_unavailable()) return ELISA_AUDIO_DEVICE_UNAVAILABLE;
    const bool initialized = silent
        ? state.service.initialize_null(sample_rate, channels)
        : state.service.initialize_default(sample_rate, channels);
    if (!initialized) return ELISA_AUDIO_DEVICE_UNAVAILABLE;
#if defined(ELISA_AUDIO_TEST_PROBE)
    if (state.fail_next_initialize_after_open) {
        state.fail_next_initialize_after_open = false;
        state.service.shutdown();
        return ELISA_AUDIO_DEVICE_UNAVAILABLE;
    }
#endif
    state.initialized = true;
    return ELISA_AUDIO_OK;
}

void shutdown_audio() {
    AudioService& state = audio_service();
    state.service.shutdown();
    state.initialized = false;
}

int32_t require_audio_service() {
    const int32_t owner_status = require_application_owner();
    if (owner_status != ELISA_AUDIO_OK) return owner_status;
    return audio_service().initialized ? ELISA_AUDIO_OK : ELISA_AUDIO_INVALID_STATE;
}

} // namespace

extern "C" int32_t elisa_audio_v1_initialize_silent(uint32_t sample_rate, uint32_t channels) {
    return initialize_audio(sample_rate, channels, true);
}

extern "C" int32_t elisa_audio_v1_initialize_default(uint32_t sample_rate, uint32_t channels) {
    return initialize_audio(sample_rate, channels, false);
}

extern "C" int32_t elisa_audio_v1_probe_provider(int32_t provider) {
    const int32_t owner_status = require_application_owner();
    if (owner_status != ELISA_AUDIO_OK) return owner_status;
    if (provider != ELISA_AUDIO_PROVIDER_SILENT && provider != ELISA_AUDIO_PROVIDER_DEFAULT) {
        return ELISA_AUDIO_INVALID_ARGUMENT;
    }
    AudioService& state = audio_service();
    if (state.initialized) return ELISA_AUDIO_INVALID_STATE;
    if (provider == ELISA_AUDIO_PROVIDER_DEFAULT && default_device_forced_unavailable()) {
        return ELISA_AUDIO_DEVICE_UNAVAILABLE;
    }

    probe::audio::Service candidate;
    bool available = false;
    try {
        available = provider == ELISA_AUDIO_PROVIDER_SILENT
            ? candidate.initialize_null(ELISA_AUDIO_MIN_SAMPLE_RATE_HZ, ELISA_AUDIO_MIN_CHANNEL_COUNT)
            : candidate.initialize_default(ELISA_AUDIO_MIN_SAMPLE_RATE_HZ, ELISA_AUDIO_MIN_CHANNEL_COUNT);
    } catch (...) {
        candidate.shutdown();
        return ELISA_AUDIO_DEVICE_UNAVAILABLE;
    }
    if (!available) {
        candidate.shutdown();
        return ELISA_AUDIO_DEVICE_UNAVAILABLE;
    }
#if defined(ELISA_AUDIO_TEST_PROBE)
    if (state.fail_next_initialize_after_open) {
        state.fail_next_initialize_after_open = false;
        candidate.shutdown();
        return ELISA_AUDIO_DEVICE_UNAVAILABLE;
    }
#endif
    candidate.shutdown();
    return ELISA_AUDIO_OK;
}

extern "C" int32_t elisa_audio_v1_take_device_recovery_request(void) {
    const int32_t owner_status = require_application_owner();
    if (owner_status != ELISA_AUDIO_OK) return owner_status;
    AudioService& state = audio_service();
    if (!state.initialized) return ELISA_AUDIO_INVALID_STATE;
    return state.service.take_device_recovery_request()
        ? ELISA_AUDIO_RECOVERY_REQUESTED : ELISA_AUDIO_RECOVERY_NOT_REQUESTED;
}

extern "C" int32_t elisa_audio_v1_recover_with_silent_device(void) {
    const int32_t owner_status = require_application_owner();
    if (owner_status != ELISA_AUDIO_OK) return owner_status;
    AudioService& state = audio_service();
    if (!state.initialized) return ELISA_AUDIO_INVALID_STATE;
    if (state.service.reopen_null()) return ELISA_AUDIO_OK;
    state.initialized = false;
    return ELISA_AUDIO_DEVICE_UNAVAILABLE;
}

#if defined(ELISA_AUDIO_TEST_PROBE)
extern "C" int32_t elisa_audio_v1_test_fail_next_initialize_after_open(void) {
    const int32_t owner_status = require_application_owner();
    if (owner_status != ELISA_AUDIO_OK) return owner_status;
    AudioService& state = audio_service();
    if (state.initialized || state.fail_next_initialize_after_open) return ELISA_AUDIO_INVALID_STATE;
    state.fail_next_initialize_after_open = true;
    return ELISA_AUDIO_OK;
}

extern "C" int32_t elisa_audio_v1_test_request_device_recovery(void) {
    const int32_t owner_status = require_application_owner();
    if (owner_status != ELISA_AUDIO_OK) return owner_status;
    AudioService& state = audio_service();
    if (!state.initialized) return ELISA_AUDIO_INVALID_STATE;
    state.service.request_device_recovery_for_test();
    return ELISA_AUDIO_OK;
}
#endif

extern "C" int32_t elisa_audio_v1_decode_file(
    const char* path, uint32_t* slot, uint32_t* generation) {
    if (path == nullptr || slot == nullptr || generation == nullptr) return ELISA_AUDIO_INVALID_ARGUMENT;
    const size_t path_length = std::strlen(path);
    if (path_length == 0 || path_length > 4096) return ELISA_AUDIO_INVALID_ARGUMENT;
    const int32_t status = require_audio_service();
    if (status != ELISA_AUDIO_OK) return status;
    const probe::audio::ClipHandle handle = audio_service().service.decode_clip_file(path);
    if (handle.slot >= probe::audio::MAX_CLIPS) return ELISA_AUDIO_DECODE_FAILED;
    *slot = handle.slot;
    *generation = handle.generation;
    return ELISA_AUDIO_OK;
}

extern "C" int32_t elisa_audio_v1_release_clip(uint32_t slot, uint32_t generation) {
    const int32_t status = require_audio_service();
    if (status != ELISA_AUDIO_OK) return status;
    switch (audio_service().service.release_clip(probe::audio::ClipHandle{slot, generation})) {
    case probe::audio::ClipReleaseStatus::Released:
        return ELISA_AUDIO_OK;
    case probe::audio::ClipReleaseStatus::InvalidHandle:
        return ELISA_AUDIO_INVALID_HANDLE;
    case probe::audio::ClipReleaseStatus::InUse:
        return ELISA_AUDIO_CLIP_IN_USE;
    }
    return ELISA_AUDIO_INVALID_HANDLE;
}

extern "C" int32_t elisa_audio_v1_play(
    uint32_t clip_slot, uint32_t clip_generation, int32_t looped, int32_t bus,
    float gain, uint32_t priority, uint32_t* slot, uint32_t* generation) {
    if (slot == nullptr || generation == nullptr || (looped != 0 && looped != 1) ||
        bus < ELISA_AUDIO_BUS_MUSIC || bus > ELISA_AUDIO_BUS_UI ||
        !std::isfinite(gain) || gain < 0.0f || gain > 4.0f) {
        return ELISA_AUDIO_INVALID_ARGUMENT;
    }
    const int32_t status = require_audio_service();
    if (status != ELISA_AUDIO_OK) return status;
    const probe::audio::ClipHandle clip{clip_slot, clip_generation};
    if (!audio_service().service.clip_live(clip)) return ELISA_AUDIO_INVALID_HANDLE;
    const probe::audio::VoiceHandle handle = audio_service().service.play(
        clip, looped != 0,
        static_cast<probe::audio::Bus>(bus), gain, priority);
    if (handle.slot >= probe::audio::MAX_VOICES) return ELISA_AUDIO_CAPACITY;
    *slot = handle.slot;
    *generation = handle.generation;
    return ELISA_AUDIO_OK;
}

extern "C" int32_t elisa_audio_v1_stop(uint32_t slot, uint32_t generation) {
    const int32_t status = require_audio_service();
    if (status != ELISA_AUDIO_OK) return status;
    return audio_service().service.stop(probe::audio::VoiceHandle{slot, generation})
        ? ELISA_AUDIO_OK : ELISA_AUDIO_INVALID_HANDLE;
}

extern "C" int32_t elisa_audio_v1_set_voice_spatial(
    uint32_t slot, uint32_t generation, float gain, float pitch_ratio) {
    if (!std::isfinite(gain) || gain < 0.0f || gain > 1.0f || !std::isfinite(pitch_ratio) ||
        pitch_ratio < probe::audio::MIN_PITCH_RATIO || pitch_ratio > probe::audio::MAX_PITCH_RATIO) {
        return ELISA_AUDIO_INVALID_ARGUMENT;
    }
    const int32_t status = require_audio_service();
    if (status != ELISA_AUDIO_OK) return status;
    return audio_service().service.set_voice_spatial_mix(
        probe::audio::VoiceHandle{slot, generation}, gain, pitch_ratio)
        ? ELISA_AUDIO_OK : ELISA_AUDIO_INVALID_HANDLE;
}

extern "C" int32_t elisa_audio_v1_set_bus_gain(int32_t bus, float gain) {
    if (bus < ELISA_AUDIO_BUS_MUSIC || bus > ELISA_AUDIO_BUS_UI ||
        !std::isfinite(gain) || gain < 0.0f || gain > 4.0f) {
        return ELISA_AUDIO_INVALID_ARGUMENT;
    }
    const int32_t status = require_audio_service();
    if (status != ELISA_AUDIO_OK) return status;
    return audio_service().service.set_bus_gain(static_cast<probe::audio::Bus>(bus), gain)
        ? ELISA_AUDIO_OK : ELISA_AUDIO_INVALID_ARGUMENT;
}

extern "C" int32_t elisa_audio_v1_active_voice_count(void) {
    const int32_t status = require_audio_service();
    if (status != ELISA_AUDIO_OK) return status;
    return static_cast<int32_t>(audio_service().service.active_voices());
}

extern "C" int32_t elisa_audio_v1_set_voice_budget(int32_t bus, uint32_t budget) {
    if (bus < ELISA_AUDIO_BUS_MUSIC || bus > ELISA_AUDIO_BUS_UI || budget > probe::audio::MAX_VOICES) {
        return ELISA_AUDIO_INVALID_ARGUMENT;
    }
    const int32_t status = require_audio_service();
    if (status != ELISA_AUDIO_OK) return status;
    return audio_service().service.set_voice_budget(static_cast<probe::audio::Bus>(bus), budget)
        ? ELISA_AUDIO_OK : ELISA_AUDIO_INVALID_STATE;
}

extern "C" int32_t elisa_audio_v1_set_bus_paused(int32_t bus, int32_t paused) {
    if (bus < ELISA_AUDIO_BUS_MUSIC || bus > ELISA_AUDIO_BUS_UI || (paused != 0 && paused != 1)) {
        return ELISA_AUDIO_INVALID_ARGUMENT;
    }
    const int32_t status = require_audio_service();
    if (status != ELISA_AUDIO_OK) return status;
    return audio_service().service.set_bus_paused(static_cast<probe::audio::Bus>(bus), paused != 0)
        ? ELISA_AUDIO_OK : ELISA_AUDIO_INVALID_STATE;
}

extern "C" int32_t elisa_audio_v1_open_stream(const char* path, int32_t looped, int32_t bus,
    float gain, uint32_t* slot, uint32_t* generation) {
    if (path == nullptr || slot == nullptr || generation == nullptr || (looped != 0 && looped != 1) ||
        bus < ELISA_AUDIO_BUS_MUSIC || bus > ELISA_AUDIO_BUS_UI ||
        !std::isfinite(gain) || gain < 0.0f || gain > 4.0f) {
        return ELISA_AUDIO_INVALID_ARGUMENT;
    }
    const size_t path_length = std::strlen(path);
    if (path_length == 0 || path_length > 4096) return ELISA_AUDIO_INVALID_ARGUMENT;
    const int32_t status = require_audio_service();
    if (status != ELISA_AUDIO_OK) return status;
    probe::audio::StreamOpenStatus opened = probe::audio::StreamOpenStatus::DecodeFailed;
    const probe::audio::StreamHandle handle = audio_service().service.open_stream(
        path, looped != 0, static_cast<probe::audio::Bus>(bus), gain, opened);
    if (opened == probe::audio::StreamOpenStatus::Capacity) return ELISA_AUDIO_CAPACITY;
    if (opened != probe::audio::StreamOpenStatus::Opened) return ELISA_AUDIO_DECODE_FAILED;
    *slot = handle.slot;
    *generation = handle.generation;
    return ELISA_AUDIO_OK;
}

extern "C" int32_t elisa_audio_v1_stop_stream(uint32_t slot, uint32_t generation) {
    const int32_t status = require_audio_service();
    if (status != ELISA_AUDIO_OK) return status;
    return audio_service().service.stop_stream(probe::audio::StreamHandle{slot, generation})
        ? ELISA_AUDIO_OK : ELISA_AUDIO_INVALID_HANDLE;
}

extern "C" int32_t elisa_audio_v1_pump_streams(void) {
    const int32_t status = require_audio_service();
    if (status != ELISA_AUDIO_OK) return status;
    return audio_service().service.pump_streams() ? ELISA_AUDIO_OK : ELISA_AUDIO_DECODE_FAILED;
}

extern "C" int32_t elisa_audio_v1_stream_status(uint32_t slot, uint32_t generation, int32_t* state,
    uint64_t* frames_played, uint64_t* underrun_frames) {
    if (state == nullptr || frames_played == nullptr || underrun_frames == nullptr) {
        return ELISA_AUDIO_INVALID_ARGUMENT;
    }
    const int32_t status = require_audio_service();
    if (status != ELISA_AUDIO_OK) return status;
    const probe::audio::StreamStatus stream =
        audio_service().service.stream_status(probe::audio::StreamHandle{slot, generation});
    if (stream.state == probe::audio::StreamState::Invalid) return ELISA_AUDIO_INVALID_HANDLE;
    *state = stream.state == probe::audio::StreamState::Finished
        ? ELISA_AUDIO_STREAM_FINISHED : ELISA_AUDIO_STREAM_PLAYING;
    *frames_played = stream.frames_played;
    *underrun_frames = stream.underrun_frames;
    return ELISA_AUDIO_OK;
}

extern "C" int32_t elisa_audio_v1_set_stream_gain(uint32_t slot, uint32_t generation, float gain) {
    if (!std::isfinite(gain) || gain < 0.0f || gain > 4.0f) return ELISA_AUDIO_INVALID_ARGUMENT;
    const int32_t status = require_audio_service();
    if (status != ELISA_AUDIO_OK) return status;
    return audio_service().service.set_stream_gain(probe::audio::StreamHandle{slot, generation}, gain)
        ? ELISA_AUDIO_OK : ELISA_AUDIO_INVALID_HANDLE;
}

extern "C" int32_t elisa_audio_v1_seek_stream(uint32_t slot, uint32_t generation, uint64_t frame) {
    const int32_t status = require_audio_service();
    if (status != ELISA_AUDIO_OK) return status;
    switch (audio_service().service.seek_stream(probe::audio::StreamHandle{slot, generation}, frame)) {
    case probe::audio::StreamSeekStatus::Seeked: return ELISA_AUDIO_OK;
    case probe::audio::StreamSeekStatus::Ended: return ELISA_AUDIO_STREAM_ENDED;
    case probe::audio::StreamSeekStatus::OutOfRange: return ELISA_AUDIO_INVALID_ARGUMENT;
    case probe::audio::StreamSeekStatus::DecodeFailed: return ELISA_AUDIO_DECODE_FAILED;
    case probe::audio::StreamSeekStatus::Invalid: break;
    }
    return ELISA_AUDIO_INVALID_HANDLE;
}

extern "C" int32_t elisa_audio_v1_stream_position(uint32_t slot, uint32_t generation, uint64_t* position) {
    if (position == nullptr) return ELISA_AUDIO_INVALID_ARGUMENT;
    const int32_t status = require_audio_service();
    if (status != ELISA_AUDIO_OK) return status;
    const probe::audio::StreamStatus stream =
        audio_service().service.stream_status(probe::audio::StreamHandle{slot, generation});
    if (stream.state == probe::audio::StreamState::Invalid) return ELISA_AUDIO_INVALID_HANDLE;
    *position = stream.position;
    return ELISA_AUDIO_OK;
}

// A frame past the clip end is an invalid argument; a stale voice is an
// invalid handle.
extern "C" int32_t elisa_audio_v1_seek_voice(uint32_t slot, uint32_t generation, uint64_t frame) {
    const int32_t status = require_audio_service();
    if (status != ELISA_AUDIO_OK) return status;
    auto& service = audio_service().service;
    const probe::audio::VoiceHandle voice{slot, generation};
    if (!service.voice_live(voice)) return ELISA_AUDIO_INVALID_HANDLE;
    return service.seek_voice(voice, frame) ? ELISA_AUDIO_OK : ELISA_AUDIO_INVALID_ARGUMENT;
}

extern "C" int32_t elisa_audio_v1_voice_frame(uint32_t slot, uint32_t generation, uint64_t* frame) {
    if (frame == nullptr) return ELISA_AUDIO_INVALID_ARGUMENT;
    const int32_t status = require_audio_service();
    if (status != ELISA_AUDIO_OK) return status;
    const uint64_t cursor = audio_service().service.voice_frame(probe::audio::VoiceHandle{slot, generation});
    if (cursor == UINT64_MAX) return ELISA_AUDIO_INVALID_HANDLE;
    *frame = cursor;
    return ELISA_AUDIO_OK;
}

extern "C" int32_t elisa_audio_v1_clip_frames(uint32_t slot, uint32_t generation, uint64_t* frames) {
    if (frames == nullptr) return ELISA_AUDIO_INVALID_ARGUMENT;
    const int32_t status = require_audio_service();
    if (status != ELISA_AUDIO_OK) return status;
    const uint64_t length = audio_service().service.clip_frames(probe::audio::ClipHandle{slot, generation});
    if (length == 0) return ELISA_AUDIO_INVALID_HANDLE;
    *frames = length;
    return ELISA_AUDIO_OK;
}

extern "C" int32_t elisa_audio_v1_active_stream_count(void) {
    const int32_t status = require_audio_service();
    if (status != ELISA_AUDIO_OK) return status;
    return static_cast<int32_t>(audio_service().service.active_streams());
}

extern "C" int32_t elisa_audio_v1_contended_callbacks(uint64_t* count) {
    if (count == nullptr) return ELISA_AUDIO_INVALID_ARGUMENT;
    const int32_t status = require_audio_service();
    if (status != ELISA_AUDIO_OK) return status;
    *count = audio_service().service.contended_callbacks();
    return ELISA_AUDIO_OK;
}

extern "C" int32_t elisa_audio_v1_recover_device(int32_t prefer_default) {
    if (prefer_default != 0 && prefer_default != 1) return ELISA_AUDIO_INVALID_ARGUMENT;
    const int32_t status = require_audio_service();
    if (status != ELISA_AUDIO_OK) return status;
    AudioService& state = audio_service();
    const bool try_default = prefer_default == 1 && !default_device_forced_unavailable();
    const int route = try_default ? state.service.reopen_preferring_default()
        : (state.service.reopen_null() ? 1 : 0);
    if (route == 2) return ELISA_AUDIO_PROVIDER_DEFAULT;
    if (route == 1) return ELISA_AUDIO_PROVIDER_SILENT;
    state.initialized = false;
    return ELISA_AUDIO_DEVICE_UNAVAILABLE;
}

extern "C" int32_t elisa_audio_v1_shutdown(void) {
    const int32_t owner_status = require_application_owner();
    if (owner_status != ELISA_AUDIO_OK) return owner_status;
    shutdown_audio();
    return ELISA_AUDIO_OK;
}

extern "C" void elisa_audio_v1_shutdown_from_application(void) {
    shutdown_audio();
}
