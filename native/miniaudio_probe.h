#pragma once
// miniaudio integration, the plan's selected audio library. The probe decodes
// the same short clip the Wicked audio check uses and opens a null playback
// device, so the library is exercised without needing an output device in a
// headless run. The implementation is compiled into this one translation unit
// (the native probe), which is what miniaudio's single-header model expects.
#include "probe_core.h"

// Only WAV decoding is needed. The bundled FLAC decoder does not compile
// cleanly with this compiler at -O0, and the engine does not use it, so the
// unused codecs are compiled out rather than patched.
#define MA_NO_FLAC
#define MA_NO_MP3
#define MA_NO_VORBIS
#define MA_NO_OPUS
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include "miniaudio_service.h"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace probe {

inline bool probe_miniaudio() {
    const int samples = 400;
    const int rate = 8000;
    const std::vector<uint8_t> wav = make_test_wav(samples, rate);

    ma_decoder_config decoder_config = ma_decoder_config_init(ma_format_s16, 1, rate);
    ma_decoder decoder;
    if (!check(ma_decoder_init_memory(wav.data(), wav.size(), &decoder_config, &decoder) == MA_SUCCESS,
            "miniaudio decoder init")) {
        return false;
    }
    ma_uint64 frames = 0;
    ma_decoder_get_length_in_pcm_frames(&decoder, &frames);
    std::vector<int16_t> buffer(samples);
    ma_uint64 read = 0;
    const ma_result read_result = ma_decoder_read_pcm_frames(&decoder, buffer.data(), samples, &read);
    ma_decoder_uninit(&decoder);
    if (!check(read_result == MA_SUCCESS && frames == (ma_uint64)samples && read == (ma_uint64)samples,
            "miniaudio decodes the clip")) {
        return false;
    }

    ma_device_config device_config = ma_device_config_init(ma_device_type_playback);
    device_config.playback.format = ma_format_s16;
    device_config.playback.channels = 1;
    device_config.sampleRate = rate;
    ma_backend backends[] = { ma_backend_null };
    ma_context_config context_config = ma_context_config_init();
    ma_context context;
    if (!check(ma_context_init(backends, 1, &context_config, &context) == MA_SUCCESS,
            "miniaudio null context")) {
        return false;
    }
    ma_device device;
    const bool opened = ma_device_init(&context, &device_config, &device) == MA_SUCCESS;
    if (opened) {
        ma_device_start(&device);
        ma_device_uninit(&device);
    }
    const ma_backend backend = context.backend;
    ma_context_uninit(&context);
    std::fprintf(stdout, "miniaudio: frames=%llu read=%llu rate=%u channels=%u backend=%d\n",
        (unsigned long long)frames, (unsigned long long)read, (unsigned)rate, 1u, (int)backend);
    if (!check(opened && backend == ma_backend_null, "miniaudio opens a null playback device")) {
        return false;
    }

    audio::Service service;
    if (!check(!service.initialize_null(0, 1), "miniaudio service rejects an invalid device")) {
        return false;
    }
    if (!check(service.initialize_null(rate, 1), "miniaudio service initializes")) {
        return false;
    }
    const audio::ClipHandle clip = service.decode_clip(wav.data(), wav.size(), rate, 1);
    if (!check(clip.slot < audio::MAX_CLIPS, "miniaudio service decodes a clip")) {
        return false;
    }
    const audio::VoiceHandle first = service.play(clip);
    const audio::VoiceHandle second = service.play(clip, true);
    if (!check(first.slot < audio::MAX_VOICES && second.slot < audio::MAX_VOICES &&
        service.active_voices() == 2, "miniaudio service starts bounded voices")) {
        return false;
    }
    std::vector<int16_t> mixed(32);
    service.mix_for_test(mixed.data(), 32);
    bool has_signal = false;
    for (const int16_t sample : mixed) has_signal = has_signal || sample != 0;
    if (!check(has_signal, "miniaudio service mixes a decoded voice")) {
        return false;
    }
    if (!check(service.stop(first) && !service.stop(first) && service.active_voices() == 1,
            "miniaudio service rejects a stale stopped voice")) {
        return false;
    }
    std::vector<int16_t> drained(samples);
    service.mix_for_test(drained.data(), samples);
    if (!check(service.voice_live(second) && service.active_voices() == 1,
            "miniaudio service keeps looped voices alive")) {
        return false;
    }
    if (!check(service.stop(second) && service.active_voices() == 0,
            "miniaudio service stops the looped voice")) {
        return false;
    }
    service.set_listener(audio::ListenerState{{1.0f, 2.0f, 3.0f}, {0.5f, 0.0f, -0.5f}});
    const audio::ListenerState listener = service.listener();
    if (!check(listener.position[0] == 1.0f && listener.position[2] == 3.0f &&
        listener.velocity[0] == 0.5f, "miniaudio service stores listener state")) {
        return false;
    }
    if (!check(service.reopen_null(), "miniaudio service reopens the device")) {
        return false;
    }
    const audio::VoiceHandle reopened = service.play(clip);
    if (!check(reopened.slot < audio::MAX_VOICES && service.active_voices() == 1,
            "miniaudio service reuses decoded clips after reopen")) {
        return false;
    }
    std::vector<int16_t> reopened_mix(8);
    service.mix_for_test(reopened_mix.data(), 8);
    if (!check(reopened_mix[0] != 0, "miniaudio service mixes after reopen")) {
        return false;
    }
    service.shutdown();
    return check(!service.voice_live(second) && !service.voice_live(reopened),
        "miniaudio service invalidates voices on shutdown");
}

} // namespace probe
