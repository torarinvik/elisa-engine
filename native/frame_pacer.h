#pragma once

#include <cstdint>

namespace probe {

// Presentation may arrive at any cadence, while gameplay advances in bounded
// fixed steps. The accumulator is integer nanoseconds so the native boundary
// does not inherit floating-point drift from the window loop.
class FixedStepPacer {
public:
    static constexpr int64_t STEP_NANOS = 16667000;
    static constexpr int MAX_STEPS = 4;

    template <typename Step>
    int advance(int64_t elapsed_nanos, Step&& step) {
        if (elapsed_nanos < 0) {
            return 0;
        }
        const int64_t capped = elapsed_nanos > STEP_NANOS * MAX_STEPS
            ? STEP_NANOS * MAX_STEPS : elapsed_nanos;
        accumulator_ += capped;
        int steps = 0;
        while (accumulator_ >= STEP_NANOS && steps < MAX_STEPS) {
            accumulator_ -= STEP_NANOS;
            step();
            ++steps;
        }
        return steps;
    }

    int64_t accumulator_nanos() const {
        return accumulator_;
    }

    double interpolation() const {
        return static_cast<double>(accumulator_) / static_cast<double>(STEP_NANOS);
    }

private:
    int64_t accumulator_ = 0;
};

} // namespace probe
