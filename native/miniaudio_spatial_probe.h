#pragma once
// Spatial mix checks for the miniaudio service. Elisa computes per-voice gain
// and Doppler pitch from its portable policy; these checks pin down how the
// callback mixer applies them: unit pitch is bit-identical to the unspatialized
// path, pitch steps resample by exact frame ratios, invalid mixes and stale
// handles are rejected, and a reused voice slot starts from neutral state.
#include "probe_core.h"
#include "miniaudio_service.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <vector>

namespace probe {

inline std::vector<int16_t> spatial_mix_frames(audio::Service& service, uint32_t frames) {
    std::vector<int16_t> mixed(frames);
    service.mix_for_test(mixed.data(), frames);
    return mixed;
}

inline bool probe_miniaudio_spatial_mix(const std::vector<uint8_t>& wav, uint32_t rate,
    uint32_t clip_frames) {
    audio::Service service;
    if (!check(service.initialize_null(rate, 1), "spatial mix service initializes")) return false;
    const audio::ClipHandle clip = service.decode_clip(wav.data(), wav.size(), rate, 1);
    if (!check(clip.slot < audio::MAX_CLIPS, "spatial mix service decodes a clip")) return false;

    const audio::VoiceHandle reference_voice = service.play(clip);
    const std::vector<int16_t> reference = spatial_mix_frames(service, 64);
    service.stop(reference_voice);
    bool reference_signal = false;
    for (const int16_t sample : reference) reference_signal = reference_signal || sample != 0;
    if (!check(reference_signal, "spatial mix reference voice has signal")) return false;

    const audio::VoiceHandle unit = service.play(clip);
    if (!check(service.set_voice_spatial_mix(unit, 1.0f, 1.0f) &&
            spatial_mix_frames(service, 64) == reference,
            "unit spatial mix is bit-identical to the unspatialized voice")) return false;
    service.stop(unit);

    const audio::VoiceHandle quiet = service.play(clip);
    bool silent = service.set_voice_spatial_mix(quiet, 0.0f, 1.0f);
    for (const int16_t sample : spatial_mix_frames(service, 32)) silent = silent && sample == 0;
    if (!check(silent && service.voice_live(quiet),
            "zero spatial gain silences a voice without stopping it")) return false;
    service.stop(quiet);

    const audio::VoiceHandle half = service.play(clip);
    bool halved = service.set_voice_spatial_mix(half, 0.5f, 1.0f);
    const std::vector<int16_t> half_mix = spatial_mix_frames(service, 64);
    for (size_t index = 0; index < half_mix.size(); ++index) {
        halved = halved && std::abs(half_mix[index] - reference[index] / 2) <= 1;
    }
    if (!check(halved, "spatial gain scales the mixed voice")) return false;
    service.stop(half);

    // An octave up advances two source frames per output frame, so a one-shot
    // clip ends after half as many output frames and never reads past it.
    const audio::VoiceHandle up = service.play(clip);
    bool octave_up = service.set_voice_spatial_mix(up, 1.0f, 2.0f);
    const std::vector<int16_t> up_mix = spatial_mix_frames(service, 32);
    for (size_t index = 0; index < up_mix.size(); ++index) {
        octave_up = octave_up && up_mix[index] == reference[index * 2];
    }
    spatial_mix_frames(service, clip_frames / 2 - 32);
    const bool live_at_end = service.voice_live(up);
    spatial_mix_frames(service, 1);
    if (!check(octave_up && live_at_end && !service.voice_live(up),
            "Doppler pitch 2 resamples and ends a one-shot voice at half length")) return false;

    const audio::VoiceHandle down = service.play(clip);
    bool octave_down = service.set_voice_spatial_mix(down, 1.0f, 0.5f);
    const std::vector<int16_t> down_mix = spatial_mix_frames(service, 64);
    for (size_t index = 0; index + 1 < 32; ++index) {
        const int midpoint = (reference[index] + reference[index + 1]) / 2;
        octave_down = octave_down && down_mix[index * 2] == reference[index] &&
            std::abs(down_mix[index * 2 + 1] - midpoint) <= 1;
    }
    if (!check(octave_down, "Doppler pitch 0.5 interpolates between source frames")) return false;
    service.stop(down);

    const audio::VoiceHandle looped = service.play(clip, true);
    const bool looped_mix = service.set_voice_spatial_mix(looped, 1.0f, 1.5f);
    spatial_mix_frames(service, clip_frames * 3);
    if (!check(looped_mix && service.voice_live(looped),
            "a pitched looped voice wraps without ending")) return false;

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    if (!check(!service.set_voice_spatial_mix(looped, nan, 1.0f) &&
            !service.set_voice_spatial_mix(looped, 1.0f, nan) &&
            !service.set_voice_spatial_mix(looped, -0.01f, 1.0f) &&
            !service.set_voice_spatial_mix(looped, 1.01f, 1.0f) &&
            !service.set_voice_spatial_mix(looped, 1.0f, 0.49f) &&
            !service.set_voice_spatial_mix(looped, 1.0f, 2.01f) &&
            !service.set_voice_spatial_mix(looped, 1.0f, infinity),
            "spatial mix rejects non-finite and out-of-range values")) return false;
    service.stop(looped);
    if (!check(!service.set_voice_spatial_mix(looped, 1.0f, 1.0f),
            "spatial mix rejects a stopped voice")) return false;

    // Leave every non-neutral spatial field on a slot, then reuse it.
    const audio::VoiceHandle dirty = service.play(clip);
    const bool dirtied = service.set_voice_position(dirty, 100.0f, 100.0f, 100.0f) &&
        service.set_voice_occlusion(dirty, 1.0f) &&
        service.set_voice_spatial_mix(dirty, 0.0f, 2.0f);
    service.stop(dirty);
    const audio::VoiceHandle reused = service.play(clip);
    if (!check(dirtied && reused.slot == dirty.slot && reused.generation != dirty.generation &&
            spatial_mix_frames(service, 64) == reference,
            "a reused voice slot starts with neutral spatial state")) return false;
    service.stop(reused);
    service.shutdown();
    return true;
}

} // namespace probe
