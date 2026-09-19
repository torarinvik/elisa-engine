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
    return check(host.window_state().focused && !host.simulation_suspended(),
                 "SDL restore resumes simulation") &&
        check(host.window_state().resize_serial >= initial.resize_serial,
              "SDL resize serial remains monotonic");
}

} // namespace probe
