#pragma once

#include "frame_pacer.h"
#include "input_tick_queue.h"
#include "probe_support.h"

namespace probe {

inline bool probe_fixed_step_pacing() {
    InputTickQueue actions;
    int32_t action = -1;
    if (!check(actions.empty() && !actions.dequeue(action), "tick input queue starts empty")) return false;
    for (std::size_t index = 0; index < InputTickQueue::CAPACITY; ++index) {
        if (!check(actions.enqueue(static_cast<int32_t>(index)), "tick input queue accepts capacity")) return false;
    }
    if (!check(!actions.enqueue(99) && actions.size() == InputTickQueue::CAPACITY,
            "tick input queue reports overflow without dropping queued actions")) return false;
    for (std::size_t index = 0; index < InputTickQueue::CAPACITY / 2; ++index) {
        if (!check(actions.dequeue(action) && action == static_cast<int32_t>(index),
                "tick input queue preserves FIFO order")) return false;
    }
    for (std::size_t index = 0; index < InputTickQueue::CAPACITY / 2; ++index) {
        if (!check(actions.enqueue(static_cast<int32_t>(index + InputTickQueue::CAPACITY)),
                "tick input queue wraps without overwriting pending actions")) return false;
    }
    for (std::size_t index = InputTickQueue::CAPACITY / 2; index < InputTickQueue::CAPACITY * 3 / 2; ++index) {
        if (!check(actions.dequeue(action) && action == static_cast<int32_t>(index),
                "tick input queue preserves order across wrap")) return false;
    }
    if (!check(actions.empty() && !actions.enqueue(-1), "tick input queue drains and rejects invalid actions")) return false;

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
