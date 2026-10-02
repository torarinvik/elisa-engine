#pragma once

// Startup hitch timing for the application host (R13). Wicked loads every
// shader and creates its pipeline states inside the synchronous initializer,
// so `pipelines_us` is the shader/pipeline-creation hitch; the first rendered
// frame then pays for any pipeline Metal creates lazily at draw time. Later
// frames are reported as a maximum so a cold/warm comparison can tell a
// startup hitch from steady-state cost. The line is printed at shutdown only
// when ELISA_FRAME_HITCH_REPORT is set, so ordinary launches stay quiet.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace elisa::hitch {

struct FrameHitchTiming {
    std::chrono::steady_clock::time_point started{};
    int64_t pipelines_us = -1;
    int64_t first_frame_us = -1;
    int64_t later_max_us = 0;
    uint64_t frames = 0;
};

inline int64_t micros_since(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start).count();
}

inline void begin(FrameHitchTiming& timing) {
    timing = FrameHitchTiming{};
    timing.started = std::chrono::steady_clock::now();
}

inline void initialized(FrameHitchTiming& timing) {
    timing.pipelines_us = micros_since(timing.started);
}

inline void frame(FrameHitchTiming& timing, std::chrono::steady_clock::time_point frame_start) {
    const int64_t elapsed = micros_since(frame_start);
    if (timing.frames == 0) {
        timing.first_frame_us = elapsed;
    } else if (elapsed > timing.later_max_us) {
        timing.later_max_us = elapsed;
    }
    ++timing.frames;
}

inline void report(const FrameHitchTiming& timing) {
    const char* enabled = std::getenv("ELISA_FRAME_HITCH_REPORT");
    if (enabled == nullptr || enabled[0] == '\0' || timing.frames == 0) return;
    std::fprintf(stdout,
        "elisa frame hitches: pipelines_us=%lld first_frame_us=%lld later_max_us=%lld frames=%llu\n",
        (long long)timing.pipelines_us, (long long)timing.first_frame_us,
        (long long)timing.later_max_us, (unsigned long long)timing.frames);
    std::fflush(stdout);
}

} // namespace elisa::hitch
