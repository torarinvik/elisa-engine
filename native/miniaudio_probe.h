#pragma once
// miniaudio integration, the plan's selected audio library. The probe decodes
// the same short clip the Wicked audio check uses and opens a null playback
// device, so the library is exercised without needing an output device in a
// headless run. The implementation is compiled into this one translation unit
// (the native probe), which is what miniaudio's single-header model expects.
#include "probe_support.h"

// Only WAV decoding is needed. The bundled FLAC decoder does not compile
// cleanly with this compiler at -O0, and the engine does not use it, so the
// unused codecs are compiled out rather than patched.
#define MA_NO_FLAC
#define MA_NO_MP3
#define MA_NO_VORBIS
#define MA_NO_OPUS
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

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
    return check(opened && backend == ma_backend_null, "miniaudio opens a null playback device");
}

} // namespace probe
