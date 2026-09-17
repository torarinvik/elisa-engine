#pragma once
// Measured scaling sweep for the native host: frame time as the live entity
// count grows. Kept out of native/wicked_probe.cpp so that file stays under
// the 600-line limit. The plan asks for increasing scene sizes with median
// and tail latency recorded, and for the largest size to still fit the
// declared frame budget rather than only being reported.
#include "probe_support.h"
#include "wiApplication.h"
#include "wiHelper.h"
#include "wiScene.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace probe {

inline bool run_perf_sweep(wi::Application& application, wi::scene::Scene& scene,
    const std::map<std::string, std::string>& manifest) {
    std::fprintf(stdout, "perf sweep:\n");
    const int sizes[3] = {16, 128, 512};
    int budget_micros = 0;
    const auto budget_it = manifest.find("frame_budget_us");
    if (budget_it != manifest.end()) {
        budget_micros = std::stoi(budget_it->second);
    }
    int64_t worst_median = 0;
    for (int size_index = 0; size_index < 3; ++size_index) {
        const int size = sizes[size_index];
        std::vector<wi::ecs::Entity> sweep;
        for (int index = 0; index < size; ++index) {
            const auto entity = scene.Entity_CreateCube("elisa_sweep_" + std::to_string(index));
            auto* sweep_transform = scene.transforms.GetComponent(entity);
            if (sweep_transform != nullptr) {
                sweep_transform->translation_local = to_wicked_space(
                    40.0f + (float)(index % 16), 0.0f, (float)(index / 16));
                sweep_transform->SetDirty();
                sweep_transform->UpdateTransform();
            }
            sweep.push_back(entity);
        }
        std::vector<int64_t> samples;
        for (int frame = 0; frame < 12; ++frame) {
            const auto start = std::chrono::steady_clock::now();
            application.Run();
            const auto stop = std::chrono::steady_clock::now();
            samples.push_back(std::chrono::duration_cast<std::chrono::microseconds>(stop - start).count());
            wi::helper::Sleep(1000);
        }
        std::sort(samples.begin(), samples.end());
        const int64_t sweep_median = samples[samples.size() / 2];
        worst_median = sweep_median > worst_median ? sweep_median : worst_median;
        std::fprintf(stdout, "  entities=%d median_us=%lld p95_us=%lld worst_us=%lld\n",
            size, (long long)sweep_median,
            (long long)samples[(samples.size() * 95) / 100], (long long)samples.back());
        for (const auto& entity : sweep) {
            scene.Entity_Remove(entity);
        }
    }
    return check(budget_micros <= 0 || worst_median <= budget_micros, "frame budget at the largest size");
}

} // namespace probe
