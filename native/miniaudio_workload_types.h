#pragma once
#include <cstdint>

namespace probe::audio {
struct WorkloadSnapshot {
    uint32_t clips = 0;
    uint32_t voices = 0;
    uint32_t streams = 0;
    uint64_t pcm_bytes = 0;
    uint64_t ring_bytes = 0;
    uint64_t callbacks = 0;
    uint64_t contended_callbacks = 0;
};
} // namespace probe::audio
