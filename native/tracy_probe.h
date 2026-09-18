#pragma once
// Tracy profiling integration. The plan lists Tracy for early profiling once
// the runtime produces useful timing data, and the probe already measures
// frame time, churn, and iteration. This header marks a few frames and a zone
// so the client is linked and exercised in the instrumented build; a running
// Tracy server would receive the same marks. With no server attached the marks
// are dropped, so the evidence is that the client is compiled in and active,
// not a captured profile.
#include <tracy/Tracy.hpp>

#include <cstdio>

namespace probe {

inline bool probe_tracy() {
    for (int index = 0; index < 4; ++index) {
        ZoneScopedN("tracy_probe");
        FrameMark;
    }
    std::fprintf(stdout, "tracy: frames marked=4\n");
    return true;
}

} // namespace probe
