#include "../native/capture_gpu_timing.h"
#include <cstdio>
#include <limits>

int main() {
    const uint64_t maximum = std::numeric_limits<uint64_t>::max();
    const struct { uint64_t begin, end, frequency; bool valid; } cases[] = {
        {0, 0, 24000000, false},
        {0, 2817308135036, 24000000, false}, // Observed unwritten begin sample.
        {7, 7, 24000000, false},
        {8, 7, 24000000, false},
        {8, 0, 24000000, false},
        {1, 2, 0, false},
        {maximum, 0, 24000000, false},
        {maximum, maximum, 24000000, false},
        {1, 2, 24000000, true},
        {maximum - 1, maximum, 24000000, true},
        {1, maximum, 1, true},
    };
    for (const auto& sample : cases) {
        if (elisa::capture::valid_gpu_timestamp_pair(sample.begin, sample.end, sample.frequency)
                != sample.valid) return 1;
    }
    std::puts("GPU timestamp pair admission passed 11 valid/invalid samples.");
    return 0;
}
