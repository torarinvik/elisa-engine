#pragma once
// Spawn/despawn churn benchmark for the native host. The plan asks to
// benchmark spawn/despawn churn separately from frame time, and the Phase 4
// exit asks that despawn not accumulate resources. Each round creates a batch,
// removes it, and asserts the scene component counts return to the pre-batch
// baseline; the round time is recorded as median and tail. It lives in its own
// header so native/wicked_probe.cpp stays under the 600-line limit.
#include "probe_core.h"
#include "wiApplication.h"
#include "wiGraphics.h"
#include "wiScene.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <malloc/malloc.h>
#include <string>
#include <thread>
#include <vector>

namespace probe {

// Allocated heap bytes currently in use. The plan asks for allocations to be
// benchmarked separately from frame time and object counts; the churn probe
// records a steady-state delta so a batch that leaks shows up here even when
// the component counts return to baseline.
inline size_t heap_bytes_in_use() {
    malloc_statistics_t stats{};
    malloc_zone_statistics(malloc_default_zone(), &stats);
    return stats.size_in_use;
}

inline bool drain_churn_gpu_retirements(wi::Application& application) {
    wi::graphics::GraphicsDevice* device = wi::graphics::GetDevice();
    if (device == nullptr) return false;
    // The renderer's suballocator retires GPU pages by frame age. Waiting for
    // GPU completion alone doesn't advance that age, so submit enough normal
    // application frames for every in-flight buffer slot to pass.
    for (uint32_t frame = 0; frame <= wi::graphics::GraphicsDevice::GetBufferCount(); ++frame) {
        application.Run();
    }
    device->WaitForGPU();
    return true;
}

inline bool run_churn_probe(wi::Application& application, wi::scene::Scene& scene,
    const std::map<std::string, std::string>& manifest) {
    (void)manifest;
    const size_t objects_before = scene.objects.GetCount();
    const size_t meshes_before = scene.meshes.GetCount();
    const size_t transforms_before = scene.transforms.GetCount();
    const size_t heap_before = heap_bytes_in_use();
    size_t steady_heap = 0;
    size_t heap_after = 0;
    const int rounds = 8;
    const int per_round = 64;
    const char* mid_hold_text = std::getenv("ELISA_CHURN_MID_HOLD_SECONDS");
    const int mid_hold_seconds = mid_hold_text == nullptr ? 0 : std::atoi(mid_hold_text);
    const char* final_hold_text = std::getenv("ELISA_CHURN_FINAL_HOLD_SECONDS");
    const int final_hold_seconds = final_hold_text == nullptr ? 0 : std::atoi(final_hold_text);
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
        if (round == 1 || round + 1 == rounds) {
            // CreateRenderData queues suballocations for release after the
            // renderer's buffer-count grace period. Settle only at the two
            // comparison points so frames used to retire GPU pages don't
            // create noise in every churn sample.
            if (!drain_churn_gpu_retirements(application)) {
                return check(false, "churn GPU retirement drain");
            }
            const size_t settled_heap = heap_bytes_in_use();
            if (round == 1) {
                steady_heap = settled_heap;
                if (mid_hold_seconds > 0) {
                    std::fprintf(stdout, "churn heap snapshot after round 2: bytes=%zu hold_seconds=%d\n",
                        settled_heap, mid_hold_seconds);
                    std::fflush(stdout);
                    std::this_thread::sleep_for(std::chrono::seconds(mid_hold_seconds));
                }
            } else {
                heap_after = settled_heap;
                if (final_hold_seconds > 0) {
                    std::fprintf(stdout, "churn final heap snapshot: bytes=%zu hold_seconds=%d\n",
                        settled_heap, final_hold_seconds);
                    std::fflush(stdout);
                    std::this_thread::sleep_for(std::chrono::seconds(final_hold_seconds));
                }
            }
        }
    }
    std::sort(samples.begin(), samples.end());
    const long long steady_delta = (long long)heap_after - (long long)steady_heap;
    std::fprintf(stdout, "churn: rounds=%d per_round=%d baseline_objects=%u median_us=%lld p95_us=%lld worst_us=%lld\n",
        rounds, per_round, (unsigned)objects_before,
        (long long)samples[samples.size() / 2],
        (long long)samples[(samples.size() * 95) / 100],
        (long long)samples.back());
    std::fprintf(stdout, "churn memory: before_bytes=%zu steady_bytes=%zu after_bytes=%zu steady_delta_bytes=%lld\n",
        heap_before, steady_heap, heap_after, steady_delta);
    // ASan's quarantine and libc++ allocation metadata are charged to the
    // process heap statistics, so its baseline noise is larger even when the
    // scene returns to the same object counts. The sanitizer still reports
    // invalid accesses; this threshold only avoids treating that bookkeeping
    // as a gameplay leak. A per-round leak remains far above either bound.
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
    constexpr long long heap_slack_bytes = 16LL * 1024 * 1024;
#else
    constexpr long long heap_slack_bytes = 2LL * 1024 * 1024;
#endif
#else
    constexpr long long heap_slack_bytes = 2LL * 1024 * 1024;
#endif
    if (!check(steady_delta < heap_slack_bytes, "churn heap stays near its steady state")) {
        return false;
    }
    return true;
}

} // namespace probe
