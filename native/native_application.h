#pragma once

// Reusable native host lifecycle. Game clients own scene and gameplay state;
// this object owns SDL3 window setup and the Wicked frame boundary. The probe
// uses the same object as any future persistent game host, while its finite
// diagnostics remain a client concern.
#include "wiApplication.h"
#include "wiAudio.h"
#include "wiGraphics.h"
#include "wiInitializer.h"
#include "wiJobSystem.h"
#include "frame_pacer.h"
#ifdef __APPLE__
#include "Foundation/Foundation.hpp"
#endif

#include <SDL3/SDL.h>

#include <array>
#include <cstdio>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>

namespace probe {

class NativeAutoreleaseScope {
public:
    NativeAutoreleaseScope() = default;
    NativeAutoreleaseScope(const NativeAutoreleaseScope&) = delete;
    NativeAutoreleaseScope& operator=(const NativeAutoreleaseScope&) = delete;

private:
#ifdef __APPLE__
    NS::SharedPtr<NS::AutoreleasePool> pool_ =
        NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
#endif
};

class NativeApplication {
public:
    static constexpr size_t MAX_SHUTDOWN_HOOKS = 64;
    using ShutdownFunction = void (*)(void* context);

    struct LifecycleTelemetry {
        uint64_t initialization_attempts = 0;
        uint64_t successful_initializations = 0;
        uint64_t partial_startup_rollbacks = 0;
        uint64_t shutdowns = 0;
        uint64_t drained_callback_batches = 0;
        uint64_t shutdown_functions_invoked = 0;
        uint64_t rejected_shutdown_registrations = 0;
        uint64_t swapchain_recreation_attempts = 0;
        uint64_t swapchain_recreation_successes = 0;
        uint64_t swapchain_recreation_failures = 0;
        size_t peak_active_callbacks = 0;
        size_t peak_shutdown_functions = 0;
    };

    class CallbackLease {
    public:
        CallbackLease(const CallbackLease&) = delete;
        CallbackLease& operator=(const CallbackLease&) = delete;
        CallbackLease(CallbackLease&& other) noexcept
            : owner_(other.owner_), admitted_(other.admitted_) { other.owner_ = nullptr; }
        ~CallbackLease() { release(); }

        bool admitted() const { return admitted_; }
        void release() {
            if (owner_ != nullptr && admitted_) owner_->end_callback();
            owner_ = nullptr;
            admitted_ = false;
        }

    private:
        friend class NativeApplication;
        explicit CallbackLease(NativeApplication& owner)
            : owner_(&owner), admitted_(owner.begin_callback()) {}
        NativeApplication* owner_ = nullptr;
        bool admitted_ = false;
    };

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
            return !focused || minimized || pixel_width == 0 || pixel_height == 0;
        }
    };

    struct Config {
        const char* title = "elisa-engine";
        int width = 1280;
        int height = 720;
        bool hidden = false;
    };

    NativeApplication() = default;
    ~NativeApplication() { shutdown(); }
    NativeApplication(const NativeApplication&) = delete;
    NativeApplication& operator=(const NativeApplication&) = delete;

    bool initialize(const Config& config) {
        if (initialized_ || sdl_initialized_ || window_ != nullptr || application_ != nullptr ||
            config.title == nullptr || config.width <= 0 || config.height <= 0) return false;
        NativeAutoreleaseScope autorelease_scope;
        ++telemetry_.initialization_attempts;
        startup_in_progress_ = true;
        close_requested_ = false;
        window_state_ = WindowState{};
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
            std::fprintf(stderr, "native application: SDL3 initialization failed: %s\n", SDL_GetError());
            SDL_Quit();
            shutdown();
            return false;
        }
        sdl_initialized_ = true;
        if (consume_startup_fault(StartupFault::AfterSDL)) {
            shutdown();
            return false;
        }
        Uint64 flags = SDL_WINDOW_METAL;
        if (config.hidden) {
            flags |= SDL_WINDOW_HIDDEN;
        }
        window_ = SDL_CreateWindow(config.title, config.width, config.height, flags);
        if (window_ == nullptr) {
            std::fprintf(stderr, "native application: window creation failed: %s\n", SDL_GetError());
            shutdown();
            return false;
        }
        if (consume_startup_fault(StartupFault::AfterWindow)) {
            shutdown();
            return false;
        }
        const SDL_PropertiesID properties = SDL_GetWindowProperties(window_);
        void* cocoa_window = SDL_GetPointerProperty(
            properties, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
        if (cocoa_window == nullptr) {
            std::fprintf(stderr, "native application: Cocoa window handle missing\n");
            shutdown();
            return false;
        }
        try {
            application_ = std::make_unique<wi::Application>();
            application_->SetWindow(reinterpret_cast<wi::platform::window_type>(cocoa_window));
            wicked_initialize_started_ = true;
            application_->Initialize();
            wi::initializer::WaitForInitializationsToFinish();
        } catch (...) {
            std::fprintf(stderr, "native application: Wicked initialization threw\n");
            shutdown();
            return false;
        }
        if (wi::graphics::GetDevice() == nullptr) {
            std::fprintf(stderr, "native application: Wicked graphics device was not created\n");
            shutdown();
            return false;
        }
        if (consume_startup_fault(StartupFault::AfterWicked)) {
            shutdown();
            return false;
        }
        initialized_ = true;
        startup_in_progress_ = false;
        ++telemetry_.successful_initializations;
        accepting_callbacks_ = true;
        refresh_window_state();
        return true;
    }

    CallbackLease callback_scope() { return CallbackLease(*this); }

    // Fixed-capacity registration performs no allocation. The context remains
    // service-owned and must outlive shutdown; callbacks run in reverse order.
    bool add_shutdown_hook(void* context, ShutdownFunction function) {
        if (function == nullptr || shutting_down_ || shutdown_function_count_ == MAX_SHUTDOWN_HOOKS) {
            ++telemetry_.rejected_shutdown_registrations;
            return false;
        }
        shutdown_functions_[shutdown_function_count_++] = ShutdownEntry{context, function};
        if (shutdown_function_count_ > telemetry_.peak_shutdown_functions) {
            telemetry_.peak_shutdown_functions = shutdown_function_count_;
        }
        return true;
    }

    // Returns false after a real quit event. The handler receives every event
    // so a game client can retain ownership of input without duplicating the
    // SDL queue policy.
    template <typename EventHandler>
    bool poll_events(EventHandler&& handler) {
        NativeAutoreleaseScope autorelease_scope;
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

    bool run_frame() {
        if (application_ == nullptr || simulation_suspended()) return false;
        NativeAutoreleaseScope autorelease_scope;
        if (!synchronize_window_surface()) return false;
        application_->Run();
        return true;
    }

    template <typename Step>
    int advance_fixed(int64_t elapsed_nanos, Step&& step) {
        if (simulation_suspended()) {
            reset_fixed_clock();
            return 0;
        }
        NativeAutoreleaseScope autorelease_scope;
        return pacer_.advance(elapsed_nanos, std::forward<Step>(step));
    }

    void reset_fixed_clock() {
        pacer_ = FixedStepPacer{};
    }

    void request_close() {
        close_requested_ = true;
    }

    bool set_fullscreen(bool enabled) {
        NativeAutoreleaseScope autorelease_scope;
        if (window_ == nullptr || !SDL_SetWindowFullscreen(window_, enabled)) {
            return false;
        }
        window_state_.fullscreen = enabled;
        refresh_window_state();
        return true;
    }

    // Rolls back partial startup and then releases host-owned work before SDL
    // disappears. The pinned Wicked audio hook runs while SDL is alive.
    void shutdown() {
        NativeAutoreleaseScope autorelease_scope;
        if (shutting_down_) {
            return;
        }
        const bool had_state = sdl_initialized_ || wicked_initialize_started_ || window_ != nullptr ||
            application_ != nullptr || initialized_ || startup_in_progress_ || shutdown_function_count_ != 0;
        shutting_down_ = true;
        {
            std::unique_lock<std::mutex> guard(callback_lock_);
            accepting_callbacks_ = false;
            if (active_callbacks_ != 0) ++telemetry_.drained_callback_batches;
            callback_drained_.wait(guard, [this] { return active_callbacks_ == 0; });
        }
        for (size_t index = shutdown_function_count_; index > 0; --index) {
            ShutdownEntry& entry = shutdown_functions_[index - 1];
            try {
                entry.function(entry.context);
            } catch (...) {
                std::fprintf(stderr, "native application: shutdown function failed\n");
            }
            entry = {};
            ++telemetry_.shutdown_functions_invoked;
        }
        shutdown_function_count_ = 0;
        if (wicked_initialize_started_) {
            wi::jobsystem::WaitForAllJobs();
        }
        if (startup_in_progress_) {
            ++telemetry_.partial_startup_rollbacks;
            startup_in_progress_ = false;
        } else if (had_state) {
            ++telemetry_.shutdowns;
        }
        if (!sdl_initialized_ && !wicked_initialize_started_ && window_ == nullptr &&
            application_ == nullptr) {
            close_requested_ = true;
            shutting_down_ = false;
            return;
        }
        if (wicked_initialize_started_) {
            wi::audio::Shutdown();
            wicked_initialize_started_ = false;
        }
        if (wi::graphics::GetDevice() != nullptr) {
            wi::graphics::GetDevice()->WaitForGPU();
        }
        if (application_ != nullptr) {
            application_->window = nullptr;
            application_.reset();
        }
        wi::graphics::GetDevice() = nullptr;
        if (window_ != nullptr) {
            SDL_DestroyWindow(window_);
            window_ = nullptr;
        }
        if (sdl_initialized_) {
            SDL_Quit();
            sdl_initialized_ = false;
        }
        initialized_ = false;
        close_requested_ = true;
        shutting_down_ = false;
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

    const LifecycleTelemetry& telemetry() const { return telemetry_; }

    wi::Application& wicked() {
        return *application_;
    }

    const wi::Application& wicked() const {
        return *application_;
    }

private:
    enum class StartupFault : uint8_t { None, AfterSDL, AfterWindow, AfterWicked };
    struct ShutdownEntry {
        void* context = nullptr;
        ShutdownFunction function = nullptr;
    };

    friend bool probe_partial_startup_failure();
    friend bool probe_window_lifecycle(NativeApplication&);

    bool consume_startup_fault(StartupFault point) {
        if (startup_fault_ != point) return false;
        startup_fault_ = StartupFault::None;
        return true;
    }

    bool begin_callback() {
        std::lock_guard<std::mutex> guard(callback_lock_);
        if (!accepting_callbacks_) return false;
        ++active_callbacks_;
        if (active_callbacks_ > telemetry_.peak_active_callbacks) {
            telemetry_.peak_active_callbacks = active_callbacks_;
        }
        return true;
    }

    void end_callback() {
        std::lock_guard<std::mutex> guard(callback_lock_);
        if (active_callbacks_ > 0) --active_callbacks_;
        if (active_callbacks_ == 0) callback_drained_.notify_all();
    }

    void refresh_window_state() {
        if (window_ == nullptr) {
            return;
        }
        int logical_width = 0;
        int logical_height = 0;
        int pixel_width = 0;
        int pixel_height = 0;
        const int previous_display_index = window_state_.display_index;
        const float previous_display_scale = window_state_.display_scale;
        SDL_GetWindowSize(window_, &logical_width, &logical_height);
        SDL_GetWindowSizeInPixels(window_, &pixel_width, &pixel_height);
        const int display_index = SDL_GetDisplayForWindow(window_);
        const float queried_scale = SDL_GetWindowDisplayScale(window_);
        float display_scale = queried_scale > 0.0f ? queried_scale : 1.0f;
        if (application_ != nullptr && application_->window != nullptr) {
            wi::platform::WindowProperties native_metrics;
            wi::platform::GetWindowProperties(application_->window, &native_metrics);
            if (native_metrics.width > 0) pixel_width = native_metrics.width;
            if (native_metrics.height > 0) pixel_height = native_metrics.height;
            if (native_metrics.dpi > 0.0f) display_scale = native_metrics.dpi / 96.0f;
        }
        if (logical_width != window_state_.logical_width ||
            logical_height != window_state_.logical_height ||
            pixel_width != window_state_.pixel_width ||
            pixel_height != window_state_.pixel_height ||
            display_index != previous_display_index ||
            display_scale != previous_display_scale) {
            ++window_state_.resize_serial;
        }
        window_state_.logical_width = logical_width;
        window_state_.logical_height = logical_height;
        window_state_.pixel_width = pixel_width;
        window_state_.pixel_height = pixel_height;
        window_state_.display_index = display_index;
        window_state_.display_scale = display_scale;
        window_state_.fullscreen = (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) != 0;
    }

    bool synchronize_window_surface() {
        if (application_ == nullptr || window_ == nullptr ||
            window_state_.pixel_width <= 0 || window_state_.pixel_height <= 0) return false;
        wi::graphics::SwapChain& swapchain = application_->swapChain;
        const bool size_changed = swapchain.desc.width != static_cast<uint32_t>(window_state_.pixel_width) ||
            swapchain.desc.height != static_cast<uint32_t>(window_state_.pixel_height);
        const bool metrics_changed = synchronized_resize_serial_ != window_state_.resize_serial;
        if (!size_changed && !metrics_changed) return true;
        if (size_changed) {
            wi::graphics::SwapChainDesc desc = swapchain.desc;
            desc.width = static_cast<uint32_t>(window_state_.pixel_width);
            desc.height = static_cast<uint32_t>(window_state_.pixel_height);
            ++telemetry_.swapchain_recreation_attempts;
            if (wi::graphics::GetDevice() == nullptr ||
                !wi::graphics::GetDevice()->CreateSwapChain(&desc, nullptr, &swapchain)) {
                ++telemetry_.swapchain_recreation_failures;
                std::fprintf(stderr, "native application: Wicked swapchain resize to %ux%u failed\n",
                    desc.width, desc.height);
                return false;
            }
            ++telemetry_.swapchain_recreation_successes;
        }
        const float dpi = window_state_.display_scale * 96.0f;
        application_->canvas.init(static_cast<uint32_t>(window_state_.pixel_width),
            static_cast<uint32_t>(window_state_.pixel_height), dpi);
        synchronized_resize_serial_ = window_state_.resize_serial;
        return true;
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
    std::unique_ptr<wi::Application> application_;
    FixedStepPacer pacer_;
    WindowState window_state_;
    bool initialized_ = false;
    bool sdl_initialized_ = false;
    bool wicked_initialize_started_ = false;
    bool startup_in_progress_ = false;
    bool close_requested_ = false;
    bool shutting_down_ = false;
    bool accepting_callbacks_ = false;
    size_t active_callbacks_ = 0;
    std::mutex callback_lock_;
    std::condition_variable callback_drained_;
    std::array<ShutdownEntry, MAX_SHUTDOWN_HOOKS> shutdown_functions_{};
    size_t shutdown_function_count_ = 0;
    uint64_t synchronized_resize_serial_ = 0;
    LifecycleTelemetry telemetry_;
    StartupFault startup_fault_ = StartupFault::None;
};

} // namespace probe
