#include "audio_service_abi.h"

#include "application_abi.h"

#include "miniaudio_service.h"

#include <cmath>
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
    if (state.initialized) state.service.shutdown();
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

#if defined(ELISA_AUDIO_TEST_PROBE)
extern "C" int32_t elisa_audio_v1_test_fail_next_initialize_after_open(void) {
    const int32_t owner_status = require_application_owner();
    if (owner_status != ELISA_AUDIO_OK) return owner_status;
    AudioService& state = audio_service();
    if (state.initialized || state.fail_next_initialize_after_open) return ELISA_AUDIO_INVALID_STATE;
    state.fail_next_initialize_after_open = true;
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

extern "C" int32_t elisa_audio_v1_shutdown(void) {
    const int32_t owner_status = require_application_owner();
    if (owner_status != ELISA_AUDIO_OK) return owner_status;
    shutdown_audio();
    return ELISA_AUDIO_OK;
}

extern "C" void elisa_audio_v1_shutdown_from_application(void) {
    shutdown_audio();
}
