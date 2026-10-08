#pragma once

#include <cstdint>

namespace elisa::capture {

// A zero counter entry denotes an invalid/unwritten Metal counter. Never turn
// it into an elapsed span measured from GPU clock origin.
inline bool valid_gpu_timestamp_pair(uint64_t begin, uint64_t end, uint64_t frequency) {
    return begin != 0 && end > begin && frequency != 0;
}

} // namespace elisa::capture
