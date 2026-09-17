#pragma once
// Native audio check for the scene probe: decode a generated clip and play
// one instance per cue the Elisa game emitted. Kept out of
// native/wicked_probe.cpp so that file stays under the 600-line limit.
//
// Verifying decoded sample information is the real check; playback is one
// call per cue, because the engine offers no playback-progress reader that
// is meaningful for a clip this short.
#include "probe_support.h"
#include "wiAudio.h"

#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace probe {

inline bool probe_audio(const std::map<std::string, std::string>& manifest) {
    const int wav_samples = 400;
    const int wav_rate = 8000;
    const std::vector<uint8_t> wav = make_test_wav(wav_samples, wav_rate);
    wi::audio::Sound sound;
    if (!check(wi::audio::CreateSound(wav.data(), wav.size(), &sound), "audio sound decode") ||
        !check(sound.IsValid(), "audio sound valid")) {
        return false;
    }
    const wi::audio::SampleInfo info = wi::audio::GetSampleInfo(&sound);
    if (!check(info.sample_count == (size_t)wav_samples && info.sample_rate == wav_rate && info.channel_count == 1,
            "audio sample information")) {
        return false;
    }
    int cue_count = 0;
    const auto cues_it = manifest.find("audio_cues");
    if (cues_it != manifest.end()) {
        cue_count = std::stoi(cues_it->second);
    }
    int played = 0;
    for (int cue = 0; cue < cue_count; ++cue) {
        wi::audio::SoundInstance instance;
        instance.SetLooped(false);
        if (!check(wi::audio::CreateSoundInstance(&sound, &instance), "audio instance") ||
            !check(instance.IsValid(), "audio instance valid")) {
            return false;
        }
        wi::audio::Play(&instance);
        ++played;
    }
    std::fprintf(stdout, "audio: decoded samples=%u rate=%d channels=%u cue plays=%d\n",
        (unsigned)info.sample_count, info.sample_rate, info.channel_count, played);
    if (cue_count > 0 && played != cue_count) {
        return false;
    }
    return true;
}

} // namespace probe
