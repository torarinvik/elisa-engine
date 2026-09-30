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
    ELISA_AUDIO_CLIP_IN_USE = -8,
    ELISA_AUDIO_STREAM_ENDED = -9,
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
    ELISA_AUDIO_STREAM_PLAYING = 1,
    ELISA_AUDIO_STREAM_FINISHED = 2,
    ELISA_AUDIO_MAX_STREAMS = 2,
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
// Reads a small authored audio asset (a sound-event table) into caller
// storage. It needs no audio service; a missing or unreadable file is
// DECODE_FAILED and a file longer than `capacity` is CAPACITY.
int32_t elisa_audio_v1_read_asset(const char* path, uint8_t* bytes, uint32_t capacity,
    uint32_t* length);
int32_t elisa_audio_v1_release_clip(uint32_t slot, uint32_t generation);
int32_t elisa_audio_v1_play(uint32_t clip_slot, uint32_t clip_generation, int32_t looped,
    int32_t bus, float gain, uint32_t priority, uint32_t* slot, uint32_t* generation);
int32_t elisa_audio_v1_stop(uint32_t slot, uint32_t generation);
// Elisa computes spatial policy; the mixer applies gain in [0, 1] and a
// Doppler pitch ratio in [0.5, 2] to one live voice.
int32_t elisa_audio_v1_set_voice_spatial(uint32_t slot, uint32_t generation,
    float gain, float pitch_ratio);
int32_t elisa_audio_v1_set_bus_gain(int32_t bus, float gain);
int32_t elisa_audio_v1_active_voice_count(void);
int32_t elisa_audio_v1_set_voice_budget(int32_t bus, uint32_t budget);
// A paused bus keeps its voices and streams in place without advancing them.
int32_t elisa_audio_v1_set_bus_paused(int32_t bus, int32_t paused);
// Streams decode on the owner thread into fixed rings; pump once per frame.
int32_t elisa_audio_v1_open_stream(const char* path, int32_t looped, int32_t bus, float gain,
    uint32_t* slot, uint32_t* generation);
int32_t elisa_audio_v1_stop_stream(uint32_t slot, uint32_t generation);
int32_t elisa_audio_v1_pump_streams(void);
int32_t elisa_audio_v1_stream_status(uint32_t slot, uint32_t generation, int32_t* state,
    uint64_t* frames_played, uint64_t* underrun_frames);
int32_t elisa_audio_v1_set_stream_gain(uint32_t slot, uint32_t generation, float gain);
int32_t elisa_audio_v1_active_stream_count(void);
// Seek to a source frame; buffered audio is dropped. A one-shot stream that
// reached its end returns STREAM_ENDED, and a frame past the end
// INVALID_ARGUMENT. `position` is the source frame the next callback plays.
int32_t elisa_audio_v1_seek_stream(uint32_t slot, uint32_t generation, uint64_t frame);
int32_t elisa_audio_v1_stream_position(uint32_t slot, uint32_t generation, uint64_t* position);
// Voice virtualization: move a live voice to a clip frame, read the frame it
// plays next, and read a clip's length in frames at the service rate.
int32_t elisa_audio_v1_seek_voice(uint32_t slot, uint32_t generation, uint64_t frame);
int32_t elisa_audio_v1_voice_frame(uint32_t slot, uint32_t generation, uint64_t* frame);
int32_t elisa_audio_v1_clip_frames(uint32_t slot, uint32_t generation, uint64_t* frames);
// Callbacks that emitted silence instead of waiting for the owner's lock.
int32_t elisa_audio_v1_contended_callbacks(uint64_t* count);
// Reopen after device loss. With prefer_default, try the system default
// output before the null device; returns the provider code that opened.
int32_t elisa_audio_v1_recover_device(int32_t prefer_default);
int32_t elisa_audio_v1_shutdown(void);

// Playback devices, enumerated independently of the running mixer. Names are
// UTF-8 without a terminator; is_default is 1 for the system default output.
int32_t elisa_audio_v1_playback_device_count(uint32_t* count);
int32_t elisa_audio_v1_playback_device(uint32_t index, uint8_t* name, uint32_t capacity,
    uint32_t* length, int32_t* is_default);
// Opens the mixer on the playback device named exactly `name` (UTF-8, no NUL).
int32_t elisa_audio_v1_initialize_named(uint32_t sample_rate, uint32_t channels,
    const uint8_t* name, uint32_t length);

#if defined(ELISA_AUDIO_TEST_PROBE)
int32_t elisa_audio_v1_test_request_device_recovery(void);
#endif

// Called only after the application ABI has validated the owner thread.
void elisa_audio_v1_shutdown_from_application(void);

#ifdef __cplusplus
}
#endif
