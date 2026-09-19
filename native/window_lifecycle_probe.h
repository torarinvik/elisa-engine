#pragma once

#include "native_application.h"
#include "probe_support.h"

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

inline bool probe_repeated_host_lifecycle() {
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
        host.shutdown();
        if (!check(host.window() == nullptr && host.close_requested(), "repeated host shutdown")) {
            return false;
        }
    }
    std::fprintf(stdout, "repeated host lifecycle: cycles=2 passed\n");
    return true;
}

} // namespace probe
