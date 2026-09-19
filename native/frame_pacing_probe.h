#pragma once

#include "frame_pacer.h"
#include "probe_support.h"

namespace probe {

inline bool probe_fixed_step_pacing() {
    FixedStepPacer pacer;
    int ticks = 0;
    if (!check(pacer.advance(FixedStepPacer::STEP_NANOS - 1, [&] { ++ticks; }) == 0,
               "pacer holds partial tick") ||
        !check(pacer.advance(1, [&] { ++ticks; }) == 1 && ticks == 1,
               "pacer emits one fixed tick") ||
        !check(pacer.interpolation() == 0.0, "pacer interpolation resets")) {
        return false;
    }
    if (!check(pacer.advance(FixedStepPacer::STEP_NANOS * 20, [&] { ++ticks; }) ==
               FixedStepPacer::MAX_STEPS && ticks == 5,
               "pacer caps catch-up") ||
        !check(pacer.accumulator_nanos() == 0, "pacer discards excess elapsed time") ||
        !check(pacer.advance(-1, [&] { ++ticks; }) == 0 && ticks == 5,
               "pacer rejects negative elapsed time")) {
        return false;
    }
    return true;
}

} // namespace probe
