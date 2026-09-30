#pragma once

#include "probe_core.h"
#include "wiPrimitive.h"
#include "wiRenderer.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace probe {

class DebugDrawBridge {
public:
    static constexpr uint32_t MAX_COMMANDS = 128;
    static constexpr uint32_t MAX_BATCH_LINES = 16384;

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

    bool text(const XMFLOAT3& position, const std::string& value,
        const XMFLOAT4& color, bool depth_tested) {
        if (count_ >= MAX_COMMANDS || value.empty() || value.size() >= MAX_TEXT ||
            !finite(position) || !finite(color)) return false;
        texts_[count_].position = position;
        texts_[count_].value = value;
        texts_[count_].color = color;
        texts_[count_].depth_tested = depth_tested;
        texts_[count_].valid = true;
        ++count_;
        return true;
    }

    // Queues `count` lines of ten floats each (start, end, rgba) as one
    // batch, all or nothing, beside the per-command slots.
    bool line_batch(const float* values, uint32_t count, bool depth_tested) {
        if (values == nullptr || count > MAX_BATCH_LINES - uint32_t(batch_.size())) return false;
        for (uint32_t index = 0; index < count * 10; ++index) {
            if (!std::isfinite(values[index])) return false;
        }
        batch_.reserve(batch_.size() + count);
        for (uint32_t index = 0; index < count; ++index) {
            const float* v = values + index * 10;
            batch_.push_back({XMFLOAT3(v[0], v[1], v[2]), XMFLOAT3(v[3], v[4], v[5]),
                XMFLOAT4(v[6], v[7], v[8], v[9]), depth_tested, true});
        }
        return true;
    }

    uint32_t pending() const { return count_ + uint32_t(batch_.size()); }

    uint32_t flush() {
        const uint32_t flushed = pending();
        for (const LineCommand& batched : batch_) {
            wi::renderer::RenderableLine line;
            line.start = batched.start;
            line.end = batched.end;
            line.color_start = batched.color;
            line.color_end = batched.color;
            wi::renderer::DrawLine(line, batched.depth_tested);
        }
        for (uint32_t index = 0; index < count_; ++index) {
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
            if (texts_[index].valid) {
                wi::renderer::DebugTextParams params;
                params.position = texts_[index].position;
                params.color = texts_[index].color;
                params.flags = texts_[index].depth_tested ? wi::renderer::DebugTextParams::DEPTH_TEST :
                    wi::renderer::DebugTextParams::NONE;
                wi::renderer::DrawDebugText(texts_[index].value.c_str(), params);
            }
        }
        clear();
        return flushed;
    }

    void clear() {
        count_ = 0;
        batch_.clear();
        for (uint32_t index = 0; index < MAX_COMMANDS; ++index) {
            boxes_[index].valid = false;
            lines_[index].valid = false;
            texts_[index].valid = false;
        }
    }

private:
    static constexpr size_t MAX_TEXT = 128;
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
    struct TextCommand {
        XMFLOAT3 position = XMFLOAT3(0, 0, 0);
        XMFLOAT4 color = XMFLOAT4(1, 1, 1, 1);
        std::string value;
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
    std::array<TextCommand, MAX_COMMANDS> texts_{};
    std::vector<LineCommand> batch_;
    uint32_t count_ = 0;
};

inline bool probe_debug_draw_bridge() {
    DebugDrawBridge bridge;
    if (!check(bridge.box(XMFLOAT3(-1, -1, -1), XMFLOAT3(1, 1, 1),
            XMFLOAT4(0, 1, 0, 1), true) &&
        bridge.line(XMFLOAT3(-1, 0, 0), XMFLOAT3(1, 0, 0),
            XMFLOAT4(1, 0, 0, 1), false) &&
        bridge.text(XMFLOAT3(0, 1, 0), "debug", XMFLOAT4(1, 1, 0, 1), true),
        "debug draw queues primitives") ||
        !check(!bridge.box(XMFLOAT3(0, 0, 0), XMFLOAT3(NAN, 1, 1),
            XMFLOAT4(1, 1, 1, 1), true), "debug draw rejects non-finite bounds") ||
        !check(bridge.pending() == 3 && bridge.flush() == 3 && bridge.pending() == 0,
            "debug draw flushes and clears scope")) return false;
    std::vector<float> lines(300 * 10, 0.5f);
    if (!check(bridge.line_batch(lines.data(), 300, true) && bridge.pending() == 300,
            "debug draw batches lines past the command slots") ||
        !check(!bridge.line_batch(lines.data(), DebugDrawBridge::MAX_BATCH_LINES, true) &&
            bridge.pending() == 300, "debug draw rejects a batch past capacity whole")) return false;
    lines[17] = NAN;
    if (!check(!bridge.line_batch(lines.data(), 2, false) && bridge.pending() == 300,
            "debug draw rejects a non-finite batch whole") ||
        !check(bridge.flush() == 300 && bridge.pending() == 0,
            "debug draw flushes batched lines")) return false;
    return true;
}

} // namespace probe
