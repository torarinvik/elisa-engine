#pragma once
// Spawn/despawn churn benchmark for the native host. The plan asks to
// benchmark spawn/despawn churn separately from frame time, and the Phase 4
// exit asks that despawn not accumulate resources. Each round creates a batch,
// removes it, and asserts the scene component counts return to the pre-batch
// baseline; the round time is recorded as median and tail. It lives in its own
// header so native/wicked_probe.cpp stays under the 600-line limit.
#include "probe_support.h"
#include "wiScene.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace probe {

inline bool run_churn_probe(wi::scene::Scene& scene, const std::map<std::string, std::string>& manifest) {
    (void)manifest;
    const size_t objects_before = scene.objects.GetCount();
    const size_t meshes_before = scene.meshes.GetCount();
    const size_t transforms_before = scene.transforms.GetCount();
    const int rounds = 8;
    const int per_round = 64;
    std::vector<int64_t> samples;
    samples.reserve(rounds);
    for (int round = 0; round < rounds; ++round) {
        const auto start = std::chrono::steady_clock::now();
        std::vector<wi::ecs::Entity> batch;
        batch.reserve(per_round);
        for (int index = 0; index < per_round; ++index) {
            batch.push_back(scene.Entity_CreateCube("elisa_churn"));
        }
        for (const auto& entity : batch) {
            scene.Entity_Remove(entity);
        }
        const auto stop = std::chrono::steady_clock::now();
        samples.push_back(std::chrono::duration_cast<std::chrono::microseconds>(stop - start).count());
        if (!check(scene.objects.GetCount() == objects_before, "churn objects return to baseline") ||
            !check(scene.meshes.GetCount() == meshes_before, "churn meshes return to baseline") ||
            !check(scene.transforms.GetCount() == transforms_before, "churn transforms return to baseline")) {
            return false;
        }
    }
    std::sort(samples.begin(), samples.end());
    std::fprintf(stdout, "churn: rounds=%d per_round=%d baseline_objects=%u median_us=%lld p95_us=%lld worst_us=%lld\n",
        rounds, per_round, (unsigned)objects_before,
        (long long)samples[samples.size() / 2],
        (long long)samples[(samples.size() * 95) / 100],
        (long long)samples.back());
    return true;
}

} // namespace probe
