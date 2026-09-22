#pragma once

#include "probe_core.h"
#include "wiScene.h"

#include <cmath>

namespace probe {

struct NativePose {
    XMFLOAT3 position = XMFLOAT3(0, 0, 0);
    XMFLOAT4 rotation = XMFLOAT4(0, 0, 0, 1);
};

class NativePoseBuffer {
public:
    explicit NativePoseBuffer(NativePose initial) : previous_(initial), current_(initial) {}

    bool commit(NativePose pose, uint64_t tick, bool teleport) {
        if (tick <= tick_ || !finite(pose)) return false;
        previous_ = teleport ? pose : current_;
        current_ = pose;
        tick_ = tick;
        teleported_ = teleport;
        return true;
    }

    NativePose sample(float alpha) const {
        if (teleported_ || alpha <= 0.0f) return current_;
        if (alpha >= 1.0f) return current_;
        NativePose result;
        result.position = XMFLOAT3(
            previous_.position.x + (current_.position.x - previous_.position.x) * alpha,
            previous_.position.y + (current_.position.y - previous_.position.y) * alpha,
            previous_.position.z + (current_.position.z - previous_.position.z) * alpha);
        result.rotation = XMFLOAT4(
            previous_.rotation.x + (current_.rotation.x - previous_.rotation.x) * alpha,
            previous_.rotation.y + (current_.rotation.y - previous_.rotation.y) * alpha,
            previous_.rotation.z + (current_.rotation.z - previous_.rotation.z) * alpha,
            previous_.rotation.w + (current_.rotation.w - previous_.rotation.w) * alpha);
        const float length = std::sqrt(result.rotation.x * result.rotation.x +
            result.rotation.y * result.rotation.y + result.rotation.z * result.rotation.z +
            result.rotation.w * result.rotation.w);
        if (length > 0.0f) {
            result.rotation.x /= length;
            result.rotation.y /= length;
            result.rotation.z /= length;
            result.rotation.w /= length;
        }
        return result;
    }

    void clear_teleport() { teleported_ = false; }
    uint64_t tick() const { return tick_; }

private:
    static bool finite(const NativePose& pose) {
        return std::isfinite(pose.position.x) && std::isfinite(pose.position.y) &&
            std::isfinite(pose.position.z) && std::isfinite(pose.rotation.x) &&
            std::isfinite(pose.rotation.y) && std::isfinite(pose.rotation.z) &&
            std::isfinite(pose.rotation.w);
    }

    NativePose previous_;
    NativePose current_;
    uint64_t tick_ = 0;
    bool teleported_ = false;
};

inline bool probe_physics_interpolation() {
    NativePose identity;
    NativePoseBuffer buffer(identity);
    NativePose moved = identity;
    moved.position.x = 10.0f;
    if (!check(buffer.commit(moved, 1, false), "native pose first commit") ||
        !check(std::fabs(buffer.sample(0.5f).position.x - 5.0f) < 0.001f,
            "native pose interpolation") ||
        !check(!buffer.commit(identity, 1, false), "native pose duplicate tick rejection")) return false;
    if (!check(buffer.commit(identity, 2, true), "native pose teleport commit") ||
        !check(buffer.sample(0.5f).position.x == 0.0f, "native pose teleport bypass") ) return false;
    buffer.clear_teleport();
    if (!check(buffer.tick() == 2, "native pose tick publication")) return false;

    // Presentation cadence is independent of the authoritative fixed ticks:
    // two render loops sample the same committed history at different rates,
    // while neither loop can advance or fork the published tick.
    NativePoseBuffer thirty_hz(identity);
    NativePoseBuffer one_twenty_hz(identity);
    for (uint64_t tick = 1; tick <= 3; ++tick) {
        NativePose pose = identity;
        pose.position.x = static_cast<float>(tick) * 2.0f;
        if (!check(thirty_hz.commit(pose, tick, false), "thirty hertz pose commit") ||
            !check(one_twenty_hz.commit(pose, tick, false), "one hundred twenty hertz pose commit")) return false;
        for (int frame = 0; frame < 30; ++frame) {
            (void)thirty_hz.sample(static_cast<float>(frame % 10) / 10.0f);
        }
        for (int frame = 0; frame < 120; ++frame) {
            (void)one_twenty_hz.sample(static_cast<float>(frame % 20) / 20.0f);
        }
    }
    if (!check(thirty_hz.tick() == 3 && one_twenty_hz.tick() == 3,
        "render cadence preserves fixed tick") ||
        !check(std::fabs(thirty_hz.sample(1.0f).position.x -
            one_twenty_hz.sample(1.0f).position.x) < 0.001f,
            "render cadence converges to fixed pose")) return false;
    return true;
}

} // namespace probe
