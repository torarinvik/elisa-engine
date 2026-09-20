#pragma once

#include "native_application.h"
#include "probe_support.h"
#include "wiJobSystem.h"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <malloc/malloc.h>
#include <mutex>
#include <thread>
#include <vector>

namespace probe {

inline size_t lifecycle_heap_bytes_in_use() {
    malloc_statistics_t stats{};
    malloc_zone_statistics(malloc_default_zone(), &stats);
    return stats.size_in_use;
}

inline bool probe_window_lifecycle(NativeApplication& host) {
    const NativeApplication::WindowState initial = host.window_state();
    if (!check(initial.logical_width > 0 && initial.logical_height > 0,
               "SDL logical window size") ||
        !check(initial.pixel_width > 0 && initial.pixel_height > 0,
               "SDL pixel window size") ||
        !check(initial.display_scale > 0.0f, "SDL display scale") ||
        !check(initial.resize_serial > 0, "SDL initial resize serial")) {
        return false;
    }
    SDL_Event event{};
    event.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    SDL_PushEvent(&event);
    host.poll_events();
    if (!check(!host.window_state().focused && host.simulation_suspended(),
            "SDL focus loss suspends simulation")) return false;
    event.type = SDL_EVENT_WINDOW_MINIMIZED;
    SDL_PushEvent(&event);
    host.poll_events();
    if (!check(host.window_state().minimized && host.simulation_suspended(),
               "SDL minimize remains suspended")) {
        return false;
    }
    event.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
    SDL_PushEvent(&event);
    host.poll_events();
    if (!check(host.window_state().focused && host.simulation_suspended(),
            "focus gain does not resume minimized window")) return false;
    event.type = SDL_EVENT_WINDOW_RESTORED;
    SDL_PushEvent(&event);
    event.type = SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED;
    SDL_PushEvent(&event);
    host.poll_events();
    if (!check(host.window_state().focused && !host.simulation_suspended(),
                 "SDL restore resumes simulation") ||
        !check(host.window_state().resize_serial >= initial.resize_serial,
              "SDL resize serial remains monotonic")) {
        return false;
    }
    const int pixel_width = host.window_state_.pixel_width;
    const int pixel_height = host.window_state_.pixel_height;
    host.window_state_.pixel_width = 0;
    int resumed_ticks = 0;
    if (!check(host.simulation_suspended() && !host.run_frame() &&
            host.advance_fixed(FixedStepPacer::STEP_NANOS * 4, [&] { ++resumed_ticks; }) == 0 &&
            resumed_ticks == 0, "zero pixel extent suspends native rendering and ticks")) return false;
    host.window_state_.pixel_width = pixel_width;
    host.window_state_.pixel_height = pixel_height;
    if (!check(host.advance_fixed(FixedStepPacer::STEP_NANOS - 1,
                [&] { ++resumed_ticks; }) == 0 &&
            host.advance_fixed(1, [&] { ++resumed_ticks; }) == 1 && resumed_ticks == 1,
            "resume resets fixed-step backlog")) return false;
    host.reset_fixed_clock();
    if (!check(host.set_fullscreen(true), "SDL enters fullscreen") ||
        !check(host.window_state().fullscreen, "SDL fullscreen state") ||
        !check(host.set_fullscreen(false), "SDL leaves fullscreen") ||
        !check(!host.window_state().fullscreen, "SDL windowed state")) {
        return false;
    }
    return check(host.window_state().display_scale > 0.0f, "SDL restored display scale");
}

inline bool probe_partial_startup_failure() {
    NativeApplication host;
    NativeApplication::Config config;
    config.title = "elisa-partial-startup-check";
    config.width = 160;
    config.height = 120;
    config.hidden = true;
    const NativeApplication::StartupFault points[] = {
        NativeApplication::StartupFault::AfterSDL,
        NativeApplication::StartupFault::AfterWindow,
        NativeApplication::StartupFault::AfterWicked,
    };
    size_t failure_count = 0;
    for (const NativeApplication::StartupFault point : points) {
        host.startup_fault_ = point;
        if (!check(!host.initialize(config), "injected partial startup failure") ||
            !check(host.window() == nullptr && SDL_WasInit(SDL_INIT_VIDEO | SDL_INIT_EVENTS) == 0,
                "partial startup releases SDL window and subsystems") ||
            !check(wi::graphics::GetDevice() == nullptr, "partial startup clears Wicked device") ||
            !check(host.telemetry().partial_startup_rollbacks == ++failure_count,
                "partial startup rollback accounting")) {
            return false;
        }
        host.shutdown();
        if (!check(host.initialize(config), "host retries after partial startup failure")) return false;
        host.shutdown();
        if (!check(host.window() == nullptr && SDL_WasInit(SDL_INIT_VIDEO | SDL_INIT_EVENTS) == 0 &&
                host.telemetry().successful_initializations == failure_count &&
                host.telemetry().shutdowns == failure_count,
                "retry shutdown returns to process baseline")) return false;
    }
    if (!check(host.telemetry().initialization_attempts == failure_count * 2 &&
            host.telemetry().successful_initializations == failure_count &&
            host.telemetry().partial_startup_rollbacks == failure_count &&
            host.telemetry().shutdowns == failure_count,
            "partial startup lifecycle totals")) return false;
    std::fprintf(stdout, "partial native startup: SDL/window/Wicked failures rolled back and retried\n");
    return true;
}

inline bool probe_repeated_host_lifecycle() {
    if (!probe_partial_startup_failure()) return false;
    constexpr size_t lifecycle_cycles = 8;
    std::array<size_t, lifecycle_cycles> heap_samples{};
    for (size_t cycle = 0; cycle <= lifecycle_cycles; ++cycle) {
        if (cycle != 0) {
            // The previous cycle's host and test bookkeeping have left scope.
            heap_samples[cycle - 1] = lifecycle_heap_bytes_in_use();
            if (cycle == lifecycle_cycles) break;
        }
        NativeApplication host;
        NativeApplication::Config config;
        config.title = "elisa-lifecycle-check";
        config.width = 160;
        config.height = 120;
        config.hidden = true;
        if (!check(host.initialize(config), "repeated host initialize")) {
            return false;
        }
        if (!check(host.window() != nullptr && !host.close_requested(), "repeated host state")) {
            host.shutdown();
            return false;
        }
        struct HookRecord { std::vector<int>* order; int id; };
        std::array<HookRecord, NativeApplication::MAX_SHUTDOWN_HOOKS - 1> hook_records{};
        std::vector<int> hook_order;
        hook_order.reserve(hook_records.size());
        const NativeApplication::ShutdownFunction record_hook = [](void* context) {
            auto* record = static_cast<HookRecord*>(context);
            record->order->push_back(record->id);
        };
        for (size_t index = 0; index < hook_records.size(); ++index) {
            hook_records[index] = HookRecord{&hook_order, static_cast<int>(index)};
            if (!check(host.add_shutdown_hook(&hook_records[index], record_hook),
                    "bounded shutdown service registers")) {
                host.shutdown();
                return false;
            }
        }
        std::mutex callback_gate;
        std::condition_variable callback_changed;
        bool callback_entered = false;
        bool release_callback = false;
        std::atomic<bool> callback_finished = false;
        std::atomic<bool> hook_observed_drain = false;
        std::atomic<bool> callback_admission_closed = false;
        std::atomic<bool> shutdown_job_finished = false;
        wi::jobsystem::context shutdown_job_context;
        struct DrainObservation {
            std::atomic<bool>* finished;
            std::atomic<bool>* observed;
            std::atomic<bool>* job_finished;
            wi::jobsystem::context* job_context;
        };
        DrainObservation observation{
            &callback_finished, &hook_observed_drain, &shutdown_job_finished, &shutdown_job_context};
        const NativeApplication::ShutdownFunction observe_drain = [](void* context) {
            auto* state = static_cast<DrainObservation*>(context);
            state->observed->store(state->finished->load());
            wi::jobsystem::Execute(*state->job_context, [job_finished = state->job_finished](
                    const wi::jobsystem::JobArgs&) {
                job_finished->store(true);
            });
        };
        if (!check(host.add_shutdown_hook(&observation, observe_drain),
                "callback-drain service registers") ||
            !check(!host.add_shutdown_hook(nullptr, observe_drain),
                "shutdown registry rejects capacity overflow")) {
            host.shutdown();
            return false;
        }
        std::thread callback_thread([&] {
            auto lease = host.callback_scope();
            {
                std::lock_guard<std::mutex> guard(callback_gate);
                callback_entered = lease.admitted();
            }
            callback_changed.notify_all();
            if (!lease.admitted()) return;
            std::unique_lock<std::mutex> guard(callback_gate);
            callback_changed.wait(guard, [&] { return release_callback; });
            callback_finished.store(true);
            lease.release();
        });
        {
            std::unique_lock<std::mutex> guard(callback_gate);
            if (!callback_changed.wait_for(guard, std::chrono::seconds(2), [&] { return callback_entered; })) {
                release_callback = true;
                guard.unlock();
                callback_changed.notify_all();
                callback_thread.join();
                host.shutdown();
                return check(false, "shutdown callback admitted before drain");
            }
        }
        std::thread admission_watcher([&] {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (std::chrono::steady_clock::now() < deadline) {
                auto lease = host.callback_scope();
                if (!lease.admitted()) {
                    callback_admission_closed.store(true);
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            {
                std::lock_guard<std::mutex> guard(callback_gate);
                release_callback = true;
            }
            callback_changed.notify_all();
        });
        host.shutdown();
        admission_watcher.join();
        callback_thread.join();
        bool reverse_order = hook_order.size() == hook_records.size();
        for (size_t index = 0; index < hook_order.size(); ++index) {
            reverse_order = reverse_order && hook_order[index] ==
                static_cast<int>(hook_records.size() - index - 1);
        }
        const auto telemetry = host.telemetry();
        if (telemetry.peak_shutdown_functions != NativeApplication::MAX_SHUTDOWN_HOOKS ||
            telemetry.shutdown_functions_invoked != NativeApplication::MAX_SHUTDOWN_HOOKS ||
            telemetry.rejected_shutdown_registrations != 1 || telemetry.drained_callback_batches != 1 ||
            telemetry.peak_active_callbacks != 1) {
            std::fprintf(stderr, "shutdown telemetry: peak_hooks=%zu invoked=%llu rejected=%llu batches=%llu peak_callbacks=%zu\n",
                telemetry.peak_shutdown_functions,
                static_cast<unsigned long long>(telemetry.shutdown_functions_invoked),
                static_cast<unsigned long long>(telemetry.rejected_shutdown_registrations),
                static_cast<unsigned long long>(telemetry.drained_callback_batches),
                telemetry.peak_active_callbacks);
        }
        if (!check(host.window() == nullptr && host.close_requested() && reverse_order,
                "repeated host shutdown and reverse service order") ||
            !check(callback_admission_closed.load() && callback_finished.load() &&
                hook_observed_drain.load(), "shutdown closes admission and drains callbacks") ||
            !check(shutdown_job_finished.load() && !wi::jobsystem::IsBusy(shutdown_job_context),
                "shutdown drains jobs submitted by shutdown hooks") ||
            !check(telemetry.peak_shutdown_functions == NativeApplication::MAX_SHUTDOWN_HOOKS &&
                telemetry.shutdown_functions_invoked == NativeApplication::MAX_SHUTDOWN_HOOKS &&
                telemetry.rejected_shutdown_registrations == 1 && telemetry.drained_callback_batches == 1 &&
                telemetry.peak_active_callbacks == 1,
                "shutdown service and callback pressure telemetry")) {
            return false;
        }
    }
    const long long steady_delta = static_cast<long long>(heap_samples.back()) -
        static_cast<long long>(heap_samples.front());
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
    constexpr long long heap_delta_cap_bytes = 16LL * 1024 * 1024;
#else
    constexpr long long heap_delta_cap_bytes = 2LL * 1024 * 1024;
#endif
#else
    constexpr long long heap_delta_cap_bytes = 2LL * 1024 * 1024;
#endif
    std::fprintf(stdout, "repeated host lifecycle: cycles=%zu hooks=%zu capacity_rejections=1 callbacks=drained\n",
        lifecycle_cycles,
        NativeApplication::MAX_SHUTDOWN_HOOKS);
    std::fprintf(stdout, "host lifecycle heap: steady_bytes=%zu final_bytes=%zu delta_bytes=%lld\n",
        heap_samples.front(), heap_samples.back(), steady_delta);
    std::fprintf(stdout, "host lifecycle heap samples:");
    for (const size_t sample : heap_samples) {
        std::fprintf(stdout, " %zu", sample);
    }
    std::fprintf(stdout, "\n");
    return check(steady_delta < heap_delta_cap_bytes, "repeated host lifecycle stays below temporary heap cap");
}

inline int run_lifecycle_only(NativeApplication& host) {
    host.shutdown();
    const bool lifecycle_ok = probe_repeated_host_lifecycle();
    const char* hold_seconds = std::getenv("ELISA_LIFECYCLE_HOLD_SECONDS");
    if (hold_seconds != nullptr) {
        const int seconds = std::atoi(hold_seconds);
        if (seconds > 0) {
            std::fprintf(stdout, "lifecycle memory inspection hold: %d seconds\n", seconds);
            std::fflush(stdout);
            std::this_thread::sleep_for(std::chrono::seconds(seconds));
        }
    }
    return lifecycle_ok ? 0 : 1;
}

} // namespace probe
