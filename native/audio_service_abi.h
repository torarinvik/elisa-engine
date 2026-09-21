#pragma once

// Narrow application-owned audio boundary. miniaudio handles, device types,
// and backend structs remain private to the native implementation.
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    ELISA_AUDIO_OK = 0,
    ELISA_AUDIO_INVALID_ARGUMENT = -1,
    ELISA_AUDIO_INVALID_STATE = -2,
    ELISA_AUDIO_WRONG_THREAD = -3,
    ELISA_AUDIO_DEVICE_UNAVAILABLE = -4,
    ELISA_AUDIO_DECODE_FAILED = -5,
    ELISA_AUDIO_CAPACITY = -6,
    ELISA_AUDIO_INVALID_HANDLE = -7,
    ELISA_AUDIO_RECOVERY_NOT_REQUESTED = 0,
    ELISA_AUDIO_RECOVERY_REQUESTED = 1,
};

enum {
    ELISA_AUDIO_BUS_MUSIC = 0,
    ELISA_AUDIO_BUS_SFX = 1,
    ELISA_AUDIO_BUS_UI = 2,
};

enum {
    ELISA_AUDIO_PROVIDER_SILENT = 1,
    ELISA_AUDIO_PROVIDER_DEFAULT = 2,
};

enum {
    ELISA_AUDIO_MIN_SAMPLE_RATE_HZ = 8000,
    ELISA_AUDIO_MAX_SAMPLE_RATE_HZ = 192000,
    ELISA_AUDIO_MIN_CHANNEL_COUNT = 1,
    ELISA_AUDIO_MAX_CHANNEL_COUNT = 2,
};

int32_t elisa_audio_v1_initialize_silent(uint32_t sample_rate, uint32_t channels);
int32_t elisa_audio_v1_initialize_default(uint32_t sample_rate, uint32_t channels);
int32_t elisa_audio_v1_probe_provider(int32_t provider);
int32_t elisa_audio_v1_take_device_recovery_request(void);
int32_t elisa_audio_v1_recover_with_silent_device(void);
int32_t elisa_audio_v1_decode_file(const char* path, uint32_t* slot, uint32_t* generation);
int32_t elisa_audio_v1_play(uint32_t clip_slot, uint32_t clip_generation, int32_t looped,
    int32_t bus, float gain, uint32_t priority, uint32_t* slot, uint32_t* generation);
int32_t elisa_audio_v1_stop(uint32_t slot, uint32_t generation);
// Elisa computes spatial policy; the mixer applies gain in [0, 1] and a
// Doppler pitch ratio in [0.5, 2] to one live voice.
int32_t elisa_audio_v1_set_voice_spatial(uint32_t slot, uint32_t generation,
    float gain, float pitch_ratio);
int32_t elisa_audio_v1_set_bus_gain(int32_t bus, float gain);
int32_t elisa_audio_v1_active_voice_count(void);
int32_t elisa_audio_v1_shutdown(void);

#if defined(ELISA_AUDIO_TEST_PROBE)
int32_t elisa_audio_v1_test_request_device_recovery(void);
#endif

// Called only after the application ABI has validated the owner thread.
void elisa_audio_v1_shutdown_from_application(void);

#ifdef __cplusplus
}
#endif
