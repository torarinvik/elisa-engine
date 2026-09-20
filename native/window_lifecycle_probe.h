#pragma once

#include "native_application.h"
#include "probe_support.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace probe {

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
    event.type = SDL_EVENT_WINDOW_MINIMIZED;
    SDL_PushEvent(&event);
    host.poll_events();
    if (!check(!host.window_state().focused && host.simulation_suspended(),
               "SDL focus and minimize suspend")) {
        return false;
    }
    event.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
    SDL_PushEvent(&event);
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
    for (const NativeApplication::StartupFault point : points) {
        host.startup_fault_ = point;
        if (!check(!host.initialize(config), "injected partial startup failure") ||
            !check(host.window() == nullptr && SDL_WasInit(SDL_INIT_VIDEO | SDL_INIT_EVENTS) == 0,
                "partial startup releases SDL window and subsystems") ||
            !check(wi::graphics::GetDevice() == nullptr, "partial startup clears Wicked device")) {
            return false;
        }
        host.shutdown();
        if (!check(host.initialize(config), "host retries after partial startup failure")) return false;
        host.shutdown();
        if (!check(host.window() == nullptr && SDL_WasInit(SDL_INIT_VIDEO | SDL_INIT_EVENTS) == 0,
                "retry shutdown returns to process baseline")) return false;
    }
    std::fprintf(stdout, "partial native startup: SDL/window/Wicked failures rolled back and retried\n");
    return true;
}

inline bool probe_repeated_host_lifecycle() {
    if (!probe_partial_startup_failure()) return false;
    for (int cycle = 0; cycle < 2; ++cycle) {
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
        std::vector<int> hook_order;
        if (!check(host.add_shutdown_hook([&hook_order] { hook_order.push_back(1); }) &&
            host.add_shutdown_hook([&hook_order] { hook_order.push_back(2); }),
            "shutdown hooks register")) return false;
        std::mutex callback_gate;
        std::condition_variable callback_changed;
        bool callback_entered = false;
        bool release_callback = false;
        std::atomic<bool> callback_finished = false;
        std::atomic<bool> hook_observed_drain = false;
        std::atomic<bool> callback_admission_closed = false;
        if (!check(host.add_shutdown_hook([&] {
                hook_observed_drain.store(callback_finished.load());
            }), "callback-drain hook registers")) return false;
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
        if (!check(host.window() == nullptr && host.close_requested() && hook_order.size() == 2 &&
            hook_order[0] == 2 && hook_order[1] == 1, "repeated host shutdown") ||
            !check(callback_admission_closed.load() && callback_finished.load() &&
                hook_observed_drain.load(), "shutdown closes admission and drains callbacks")) {
            return false;
        }
    }
    std::fprintf(stdout, "repeated host lifecycle: cycles=2 passed\n");
    return true;
}

} // namespace probe
