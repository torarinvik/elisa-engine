#pragma once
// Measured data-layout comparison: iterating a component's contiguous storage
// versus looking each entity up by id. The plan asks for data-layout choices
// to be measured, not asserted, so this records both medians and requires the
// contiguous walk to be no worse than a loose multiple of the indexed one.
#include "probe_core.h"
#include "wiScene.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>

namespace probe {

inline bool run_layout_probe(wi::scene::Scene& scene) {
    const int count = 512;
    const int rounds = 64;
    std::vector<wi::ecs::Entity> entities;
    entities.reserve(count);
    for (int index = 0; index < count; ++index) {
        const auto entity = scene.Entity_CreateCube("elisa_layout_" + std::to_string(index));
        auto* transform = scene.transforms.GetComponent(entity);
        if (transform != nullptr) {
            transform->translation_local = to_wicked_space(80.0f + (float)(index % 16), 0.0f, (float)(index / 16));
            transform->SetDirty();
            transform->UpdateTransform();
        }
        entities.push_back(entity);
    }
    const size_t component_count = scene.transforms.GetCount();
    auto now = []() { return std::chrono::steady_clock::now(); };
    volatile float sink = 0.0f;

    std::vector<int64_t> contiguous_samples;
    std::vector<int64_t> indexed_samples;
    for (int round = 0; round < rounds; ++round) {
        const auto start = now();
        float local = 0.0f;
        for (size_t index = 0; index < component_count; ++index) {
            local += scene.transforms[index].translation_local.x;
        }
        sink = local;
        contiguous_samples.push_back(std::chrono::duration_cast<std::chrono::microseconds>(now() - start).count());
    }
    for (int round = 0; round < rounds; ++round) {
        const auto start = now();
        float local = 0.0f;
        for (const auto& entity : entities) {
            const auto* transform = scene.transforms.GetComponent(entity);
            if (transform != nullptr) {
                local += transform->translation_local.x;
            }
        }
        sink = local;
        indexed_samples.push_back(std::chrono::duration_cast<std::chrono::microseconds>(now() - start).count());
    }
    (void)sink;
    std::sort(contiguous_samples.begin(), contiguous_samples.end());
    std::sort(indexed_samples.begin(), indexed_samples.end());
    const int64_t contiguous = contiguous_samples[contiguous_samples.size() / 2];
    const int64_t indexed = indexed_samples[indexed_samples.size() / 2];
    for (const auto& entity : entities) {
        scene.Entity_Remove(entity);
    }
    std::fprintf(stdout, "layout: entities=%d contiguous_us=%lld indexed_us=%lld\n",
        count, (long long)contiguous, (long long)indexed);
    // Loose bound: the contiguous walk must not be dramatically worse; this is
    // a recorded comparison, not a claim that one layout always wins.
    return check(contiguous <= indexed * 2 + 50, "contiguous iteration is not dramatically slower than indexed");
}

} // namespace probe
