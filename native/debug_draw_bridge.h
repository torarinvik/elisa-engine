#pragma once

#include "probe_core.h"
#include "wiPrimitive.h"
#include "wiRenderer.h"

#include <array>
#include <cmath>
#include <cstdint>

namespace probe {

class DebugDrawBridge {
public:
    static constexpr uint32_t MAX_COMMANDS = 128;

    bool box(const XMFLOAT3& minimum, const XMFLOAT3& maximum,
        const XMFLOAT4& color, bool depth_tested) {
        if (count_ >= MAX_COMMANDS || !finite(minimum) || !finite(maximum) ||
            !finite(color) || minimum.x > maximum.x || minimum.y > maximum.y ||
            minimum.z > maximum.z) return false;
        boxes_[count_] = {wi::primitive::AABB(minimum, maximum), color, depth_tested, true};
        ++count_;
        return true;
    }

    bool line(const XMFLOAT3& start, const XMFLOAT3& end,
        const XMFLOAT4& color, bool depth_tested) {
        if (count_ >= MAX_COMMANDS || !finite(start) || !finite(end) || !finite(color)) return false;
        lines_[count_] = {start, end, color, depth_tested, true};
        ++count_;
        return true;
    }

    uint32_t pending() const { return count_; }

    uint32_t flush() {
        const uint32_t flushed = count_;
        for (uint32_t index = 0; index < flushed; ++index) {
            if (boxes_[index].valid) {
                wi::renderer::DrawBox(boxes_[index].bounds, boxes_[index].color,
                    boxes_[index].depth_tested);
            }
            if (lines_[index].valid) {
                wi::renderer::RenderableLine line;
                line.start = lines_[index].start;
                line.end = lines_[index].end;
                line.color_start = lines_[index].color;
                line.color_end = lines_[index].color;
                wi::renderer::DrawLine(line, lines_[index].depth_tested);
            }
        }
        clear();
        return flushed;
    }

    void clear() {
        count_ = 0;
        for (uint32_t index = 0; index < MAX_COMMANDS; ++index) {
            boxes_[index].valid = false;
            lines_[index].valid = false;
        }
    }

private:
    struct BoxCommand {
        wi::primitive::AABB bounds;
        XMFLOAT4 color = XMFLOAT4(1, 1, 1, 1);
        bool depth_tested = true;
        bool valid = false;
    };
    struct LineCommand {
        XMFLOAT3 start = XMFLOAT3(0, 0, 0);
        XMFLOAT3 end = XMFLOAT3(0, 0, 0);
        XMFLOAT4 color = XMFLOAT4(1, 1, 1, 1);
        bool depth_tested = false;
        bool valid = false;
    };

    static bool finite(const XMFLOAT3& value) {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    }
    static bool finite(const XMFLOAT4& value) {
        return std::isfinite(value.x) && std::isfinite(value.y) &&
            std::isfinite(value.z) && std::isfinite(value.w);
    }

    std::array<BoxCommand, MAX_COMMANDS> boxes_{};
    std::array<LineCommand, MAX_COMMANDS> lines_{};
    uint32_t count_ = 0;
};

inline bool probe_debug_draw_bridge() {
    DebugDrawBridge bridge;
    if (!check(bridge.box(XMFLOAT3(-1, -1, -1), XMFLOAT3(1, 1, 1),
            XMFLOAT4(0, 1, 0, 1), true) &&
        bridge.line(XMFLOAT3(-1, 0, 0), XMFLOAT3(1, 0, 0),
            XMFLOAT4(1, 0, 0, 1), false), "debug draw queues primitives") ||
        !check(!bridge.box(XMFLOAT3(0, 0, 0), XMFLOAT3(NAN, 1, 1),
            XMFLOAT4(1, 1, 1, 1), true), "debug draw rejects non-finite bounds") ||
        !check(bridge.pending() == 2 && bridge.flush() == 2 && bridge.pending() == 0,
            "debug draw flushes and clears scope")) return false;
    return true;
}

} // namespace probe
