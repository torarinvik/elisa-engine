#pragma once

// Reusable native host lifecycle. Game clients own scene and gameplay state;
// this object owns SDL3 window setup and the Wicked frame boundary. The probe
// uses the same object as any future persistent game host, while its finite
// diagnostics remain a client concern.
#include "wiApplication.h"
#include "wiGraphics.h"
#include "wiInitializer.h"
#include "frame_pacer.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <chrono>
#include <utility>

namespace probe {

class NativeApplication {
public:
    struct WindowState {
        int logical_width = 0;
        int logical_height = 0;
        int pixel_width = 0;
        int pixel_height = 0;
        int display_index = -1;
        float display_scale = 1.0f;
        uint64_t resize_serial = 0;
        bool focused = true;
        bool minimized = false;
        bool fullscreen = false;

        bool suspended() const {
            return minimized || pixel_width == 0 || pixel_height == 0;
        }
    };

    struct Config {
        const char* title = "elisa-engine";
        int width = 1280;
        int height = 720;
        bool hidden = false;
    };

    NativeApplication() = default;
    ~NativeApplication() = default;
    NativeApplication(const NativeApplication&) = delete;
    NativeApplication& operator=(const NativeApplication&) = delete;

    bool initialize(const Config& config) {
        if (initialized_) {
            return false;
        }
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
            std::fprintf(stderr, "native application: SDL3 initialization failed: %s\n", SDL_GetError());
            return false;
        }
        Uint64 flags = SDL_WINDOW_METAL;
        if (config.hidden) {
            flags |= SDL_WINDOW_HIDDEN;
        }
        window_ = SDL_CreateWindow(config.title, config.width, config.height, flags);
        if (window_ == nullptr) {
            std::fprintf(stderr, "native application: window creation failed: %s\n", SDL_GetError());
            SDL_Quit();
            return false;
        }
        const SDL_PropertiesID properties = SDL_GetWindowProperties(window_);
        void* cocoa_window = SDL_GetPointerProperty(
            properties, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
        if (cocoa_window == nullptr) {
            std::fprintf(stderr, "native application: Cocoa window handle missing\n");
            SDL_DestroyWindow(window_);
            window_ = nullptr;
            SDL_Quit();
            return false;
        }
        application_.SetWindow(reinterpret_cast<wi::platform::window_type>(cocoa_window));
        application_.Initialize();
        wi::initializer::WaitForInitializationsToFinish();
        initialized_ = true;
        close_requested_ = false;
        refresh_window_state();
        return true;
    }

    // Returns false after a real quit event. The handler receives every event
    // so a game client can retain ownership of input without duplicating the
    // SDL queue policy.
    template <typename EventHandler>
    bool poll_events(EventHandler&& handler) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            update_window_state(event);
            handler(event);
            if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                close_requested_ = true;
            }
        }
        return !close_requested_;
    }

    bool poll_events() {
        return poll_events([](const SDL_Event&) {});
    }

    void run_frame() {
        application_.Run();
    }

    template <typename Step>
    int advance_fixed(int64_t elapsed_nanos, Step&& step) {
        return pacer_.advance(elapsed_nanos, std::forward<Step>(step));
    }

    void reset_fixed_clock() {
        pacer_ = FixedStepPacer{};
    }

    void request_close() {
        close_requested_ = true;
    }

    bool set_fullscreen(bool enabled) {
        if (window_ == nullptr || !SDL_SetWindowFullscreen(window_, enabled)) {
            return false;
        }
        window_state_.fullscreen = enabled;
        refresh_window_state();
        return true;
    }

    // Releases work owned by this host before SDL disappears. Wicked's
    // process-wide worker services remain outside this wrapper; callers can
    // use this boundary to test whether the pinned build tolerates normal
    // C++ destruction instead of the finite probe's forced exit.
    void shutdown() {
        if (!initialized_) {
            return;
        }
        if (wi::graphics::GetDevice() != nullptr) {
            wi::graphics::GetDevice()->WaitForGPU();
        }
        application_.window = nullptr;
        if (window_ != nullptr) {
            SDL_DestroyWindow(window_);
            window_ = nullptr;
        }
        SDL_Quit();
        initialized_ = false;
        close_requested_ = true;
    }

    bool close_requested() const {
        return close_requested_;
    }

    SDL_Window* window() const {
        return window_;
    }

    const WindowState& window_state() const {
        return window_state_;
    }

    bool simulation_suspended() const {
        return window_state_.suspended();
    }

    wi::Application& wicked() {
        return application_;
    }

    const wi::Application& wicked() const {
        return application_;
    }

private:
    void refresh_window_state() {
        if (window_ == nullptr) {
            return;
        }
        int logical_width = 0;
        int logical_height = 0;
        int pixel_width = 0;
        int pixel_height = 0;
        SDL_GetWindowSize(window_, &logical_width, &logical_height);
        SDL_GetWindowSizeInPixels(window_, &pixel_width, &pixel_height);
        if (logical_width != window_state_.logical_width ||
            logical_height != window_state_.logical_height ||
            pixel_width != window_state_.pixel_width ||
            pixel_height != window_state_.pixel_height) {
            ++window_state_.resize_serial;
        }
        window_state_.logical_width = logical_width;
        window_state_.logical_height = logical_height;
        window_state_.pixel_width = pixel_width;
        window_state_.pixel_height = pixel_height;
        window_state_.display_index = SDL_GetDisplayForWindow(window_);
        const float scale = SDL_GetWindowDisplayScale(window_);
        window_state_.display_scale = scale > 0.0f ? scale : 1.0f;
        window_state_.fullscreen = (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) != 0;
    }

    void update_window_state(const SDL_Event& event) {
        switch (event.type) {
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            window_state_.focused = true;
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            window_state_.focused = false;
            break;
        case SDL_EVENT_WINDOW_MINIMIZED:
            window_state_.minimized = true;
            break;
        case SDL_EVENT_WINDOW_RESTORED:
            window_state_.minimized = false;
            refresh_window_state();
            break;
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
            refresh_window_state();
            break;
        default:
            break;
        }
    }

    SDL_Window* window_ = nullptr;
    wi::Application application_;
    FixedStepPacer pacer_;
    WindowState window_state_;
    bool initialized_ = false;
    bool close_requested_ = false;
};

} // namespace probe
