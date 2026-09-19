#pragma once

// Reusable native host lifecycle. Game clients own scene and gameplay state;
// this object owns SDL3 window setup and the Wicked frame boundary. The probe
// uses the same object as any future persistent game host, while its finite
// diagnostics remain a client concern.
#include "wiApplication.h"
#include "wiInitializer.h"

#include <SDL3/SDL.h>

#include <cstdio>

namespace probe {

class NativeApplication {
public:
    struct Config {
        const char* title = "elisa-engine";
        int width = 1280;
        int height = 720;
        bool hidden = false;
    };

    NativeApplication() = default;
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
        return true;
    }

    // Returns false after a real quit event. Clients may continue to poll
    // their own input events between calls when they need event ownership.
    bool poll_events() {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                close_requested_ = true;
            }
        }
        return !close_requested_;
    }

    void run_frame() {
        application_.Run();
    }

    void request_close() {
        close_requested_ = true;
    }

    bool close_requested() const {
        return close_requested_;
    }

    SDL_Window* window() const {
        return window_;
    }

    wi::Application& wicked() {
        return application_;
    }

    const wi::Application& wicked() const {
        return application_;
    }

private:
    SDL_Window* window_ = nullptr;
    wi::Application application_;
    bool initialized_ = false;
    bool close_requested_ = false;
};

} // namespace probe
