#include "application_abi.h"
#include "png_capture.h"
#include "audio_service_abi.h"
#include "application_input_codes.h"
#include "backend_capability_query.h"
#include "native_application.h"
#include "physics_service_abi.h"
#include "shader_path_validation.h"
#include "wiHelper.h"
#include "wiRenderer.h"

#include <SDL3/SDL_gamepad.h>

#include <cstddef>
#include <chrono>
#include <array>
#include <cstdint>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <limits>
#include <string>
#include <thread>

namespace {

constexpr size_t INPUT_EVENT_CAPACITY = 512;
constexpr size_t GAMEPAD_CAPACITY = 4;
struct QueuedInputEvent {
    int32_t kind = 0;
    int32_t device = 0;
    int64_t code = 0;
    float value = 0.0f;
    int32_t pressed = 0;
    int32_t released = 0;
    int32_t chord_down = 0;
};

struct OpenGamepad {
    SDL_JoystickID id = 0;
    SDL_Gamepad* handle = nullptr;
};

struct ApplicationService {
    std::mutex mutex;
    probe::NativeApplication host;
    std::thread::id owner_thread{};
    std::chrono::steady_clock::time_point initialized_at{};
    std::chrono::steady_clock::time_point previous_pump{};
    uint64_t frame_count = 0;
    uint64_t elapsed_nanos = 0;
    uint32_t pending_events = 0;
    std::array<QueuedInputEvent, INPUT_EVENT_CAPACITY> input_events{};
    std::array<OpenGamepad, GAMEPAD_CAPACITY> gamepads{};
    size_t input_event_count = 0;
    size_t input_event_read = 0;
    bool input_overflow = false;
    bool input_overflow_reported = false;
    ElisaBackendProfile backend_profile{};
    bool backend_profile_valid = false;
    bool initialized = false;
};

size_t open_gamepad_count(const ApplicationService& service) {
    size_t count = 0;
    for (const OpenGamepad& gamepad : service.gamepads) {
        if (gamepad.handle != nullptr) ++count;
    }
    return count;
}

bool has_gamepad(const ApplicationService& service, SDL_JoystickID id) {
    for (const OpenGamepad& gamepad : service.gamepads) {
        if (gamepad.handle != nullptr && gamepad.id == id) return true;
    }
    return false;
}

void queue_input_event(ApplicationService& service, int32_t kind, int32_t device,
    int64_t code, float value, bool pressed, bool released) {
    if (service.input_event_count == INPUT_EVENT_CAPACITY) {
        service.input_overflow = true;
        return;
    }
    service.input_events[service.input_event_count++] = QueuedInputEvent{
        kind, device, code, value, pressed ? 1 : 0, released ? 1 : 0, 0};
}

void open_gamepad(ApplicationService& service, SDL_JoystickID id) {
    if (has_gamepad(service, id)) return;
    const size_t count_before = open_gamepad_count(service);
    for (OpenGamepad& slot : service.gamepads) {
        if (slot.handle != nullptr) continue;
        SDL_Gamepad* handle = SDL_OpenGamepad(id);
        if (handle == nullptr) return;
        slot = OpenGamepad{id, handle};
        if (count_before == 0) {
            queue_input_event(service, ELISA_APPLICATION_INPUT_GAMEPAD_CONNECTED, probe::INPUT_DEVICE_GAMEPAD,
                0, 1.0f, true, false);
        }
        return;
    }
}

void close_gamepad(ApplicationService& service, SDL_JoystickID id) {
    for (OpenGamepad& slot : service.gamepads) {
        if (slot.handle == nullptr || slot.id != id) continue;
        SDL_CloseGamepad(slot.handle);
        slot = OpenGamepad{};
        const size_t remaining = open_gamepad_count(service);
        queue_input_event(service, ELISA_APPLICATION_INPUT_GAMEPAD_DISCONNECTED, probe::INPUT_DEVICE_GAMEPAD,
            0, 0.0f, remaining != 0, true);
        return;
    }
}

ApplicationService& application_service() {
    static ApplicationService service;
    return service;
}

bool valid_title(const char* title) {
    if (title == nullptr || title[0] == '\0') return false;
    for (size_t index = 1; index < 256; ++index) {
        if (title[index] == '\0') return true;
    }
    return false;
}

bool on_owner_thread(const ApplicationService& service) {
    return service.owner_thread == std::this_thread::get_id();
}

void configure_shader_root() {
    const char* configured = std::getenv("ELISA_ENGINE_SHADER_PATH");
    if (configured == nullptr || configured[0] == '\0') return;
    std::string path(configured);
    if (path.back() != '/') path.push_back('/');
    wi::renderer::SetShaderPath(path);
    wi::renderer::SetShaderSourcePath(path);
}

} // namespace

extern "C" uint32_t elisa_application_abi_version(void) {
    return ELISA_APPLICATION_ABI_VERSION;
}

extern "C" const char* elisa_application_v1_project_title(void) {
    const char* title = std::getenv("ELISA_PROJECT_TITLE");
    return valid_title(title) ? title : "Elisa Engine";
}

static int32_t project_dimension(const char* key, int32_t fallback) {
    const char* value = std::getenv(key);
    if (value == nullptr || value[0] == '\0') return fallback;
    char* end = nullptr;
    const long parsed = std::strtol(value, &end, 10);
    if (end == value || *end != '\0' || parsed <= 0 || parsed > 16384) return fallback;
    return static_cast<int32_t>(parsed);
}

extern "C" int32_t elisa_application_v1_project_width(void) {
    return project_dimension("ELISA_PROJECT_WIDTH", 1280);
}

extern "C" int32_t elisa_application_v1_project_height(void) {
    return project_dimension("ELISA_PROJECT_HEIGHT", 720);
}

extern "C" int32_t elisa_application_v1_project_hidden(void) {
    const char* value = std::getenv("ELISA_PROJECT_HIDDEN");
    return value != nullptr && std::strcmp(value, "1") == 0 ? 1 : 0;
}

extern "C" int32_t elisa_application_v1_initialize(
    const char* title, int32_t width, int32_t height, int32_t hidden) {
    if (!valid_title(title) || width <= 0 || height <= 0 || width > 16384 || height > 16384 ||
        (hidden != 0 && hidden != 1)) {
        return ELISA_APPLICATION_INVALID_ARGUMENT;
    }
    const char* shader_path = std::getenv("ELISA_ENGINE_SHADER_PATH");
    const char* shader_manifest = std::getenv("ELISA_ENGINE_SHADER_MANIFEST");
    if (elisa::shader::manifest_status_is_invalid(shader_path, shader_manifest)) {
        return ELISA_APPLICATION_SHADER_MANIFEST_INVALID;
    }
    if (!elisa::shader::root_is_valid(shader_path, shader_manifest)) {
        return ELISA_APPLICATION_SHADER_PATH_INVALID;
    }
    ApplicationService& service = application_service();
    std::lock_guard<std::mutex> guard(service.mutex);
    if (service.initialized) return ELISA_APPLICATION_INVALID_STATE;

    probe::NativeApplication::Config config;
    config.title = title;
    config.width = width;
    config.height = height;
    config.hidden = hidden != 0;
    configure_shader_root();
    if (!service.host.initialize(config)) return ELISA_APPLICATION_INITIALIZATION_FAILED;

    service.backend_profile_valid = probe::query_live_backend_profile(service.backend_profile);

    service.owner_thread = std::this_thread::get_id();
    service.initialized_at = std::chrono::steady_clock::now();
    service.previous_pump = std::chrono::steady_clock::now();
    service.frame_count = 0;
    service.elapsed_nanos = 0;
    service.pending_events = 0;
    service.input_event_count = 0;
    service.input_event_read = 0;
    service.input_overflow = false;
    service.input_overflow_reported = false;
    service.initialized = true;
    return ELISA_APPLICATION_OK;
}

extern "C" int32_t elisa_application_v1_backend_profile(
    int64_t* capabilities, int64_t* optional, int64_t* max_viewports,
    int64_t* max_workers, int64_t* memory_budget, int64_t* memory_usage,
    int64_t* formats, int64_t* graphics_workers, int64_t* streaming_workers,
    int64_t* service_version) {
    if (capabilities == nullptr || optional == nullptr || max_viewports == nullptr ||
        max_workers == nullptr || memory_budget == nullptr || memory_usage == nullptr ||
        formats == nullptr || graphics_workers == nullptr || streaming_workers == nullptr ||
        service_version == nullptr) {
        return ELISA_APPLICATION_INVALID_ARGUMENT;
    }
    ApplicationService& service = application_service();
    std::lock_guard<std::mutex> guard(service.mutex);
    if (!service.initialized) return ELISA_APPLICATION_INVALID_STATE;
    if (!on_owner_thread(service)) return ELISA_APPLICATION_WRONG_THREAD;
    if (!service.backend_profile_valid ||
        service.backend_profile.memory_budget_bytes > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) ||
        service.backend_profile.memory_usage_bytes > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
        return ELISA_APPLICATION_PROFILE_UNAVAILABLE;
    }
    const ElisaBackendProfile& profile = service.backend_profile;
    *capabilities = static_cast<int64_t>(profile.capability_bits);
    *optional = static_cast<int64_t>(profile.optional_bits);
    *max_viewports = profile.max_viewports;
    *max_workers = profile.max_workers;
    *memory_budget = static_cast<int64_t>(profile.memory_budget_bytes);
    *memory_usage = static_cast<int64_t>(profile.memory_usage_bytes);
    *formats = static_cast<int64_t>(profile.resource_format_bits);
    *graphics_workers = profile.graphics_workers;
    *streaming_workers = profile.streaming_workers;
    *service_version = profile.abi_version;
    return ELISA_APPLICATION_OK;
}

extern "C" int32_t elisa_application_v1_fallback_provider_available(int32_t provider) {
    ApplicationService& service = application_service();
    {
        std::lock_guard<std::mutex> guard(service.mutex);
        if (!service.initialized) return ELISA_APPLICATION_INVALID_STATE;
        if (!on_owner_thread(service)) return ELISA_APPLICATION_WRONG_THREAD;
    }

    int32_t status = ELISA_APPLICATION_INVALID_ARGUMENT;
    switch (provider) {
        case ELISA_APPLICATION_PROVIDER_JOLT_PHYSICS:
            status = elisa_physics_v1_probe_provider();
            break;
        case ELISA_APPLICATION_PROVIDER_MINIAUDIO_SILENT:
            status = elisa_audio_v1_probe_provider(ELISA_AUDIO_PROVIDER_SILENT);
            break;
        case ELISA_APPLICATION_PROVIDER_MINIAUDIO_DEFAULT:
            status = elisa_audio_v1_probe_provider(ELISA_AUDIO_PROVIDER_DEFAULT);
            break;
        default:
            return ELISA_APPLICATION_INVALID_ARGUMENT;
    }
    if (status == ELISA_PHYSICS_WRONG_THREAD || status == ELISA_AUDIO_WRONG_THREAD) {
        return ELISA_APPLICATION_WRONG_THREAD;
    }
    return status == ELISA_PHYSICS_OK || status == ELISA_AUDIO_OK ? 1 : 0;
}

extern "C" int32_t elisa_application_v1_pump(void) {
    ApplicationService& service = application_service();
    std::lock_guard<std::mutex> guard(service.mutex);
    if (!service.initialized) return ELISA_APPLICATION_INVALID_STATE;
    if (!on_owner_thread(service)) return ELISA_APPLICATION_WRONG_THREAD;
    const auto now = std::chrono::steady_clock::now();
    service.elapsed_nanos = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(now - service.previous_pump).count());
    service.previous_pump = now;
    const bool should_continue = service.host.poll_events([&service](const SDL_Event& event) {
        switch (event.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            service.pending_events |= ELISA_APPLICATION_EVENT_CLOSE_REQUESTED;
            break;
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
            service.pending_events |= ELISA_APPLICATION_EVENT_RESIZED;
            break;
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            service.pending_events |= ELISA_APPLICATION_EVENT_FOCUS_GAINED;
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            service.pending_events |= ELISA_APPLICATION_EVENT_FOCUS_LOST;
            queue_input_event(service, ELISA_APPLICATION_INPUT_FOCUS_LOST, probe::INPUT_DEVICE_GLOBAL, 0, 0.0f, false, true);
            break;
        case SDL_EVENT_KEY_DOWN:
            if (const int32_t code = probe::keyboard_key_code(event.key.key); code != 0) {
                queue_input_event(service, ELISA_APPLICATION_INPUT_KEY, probe::INPUT_DEVICE_KEYBOARD,
                    code, 1.0f, true, false);
            }
            break;
        case SDL_EVENT_KEY_UP:
            if (const int32_t code = probe::keyboard_key_code(event.key.key); code != 0) {
                queue_input_event(service, ELISA_APPLICATION_INPUT_KEY, probe::INPUT_DEVICE_KEYBOARD,
                    code, 0.0f, false, true);
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            queue_input_event(service, ELISA_APPLICATION_INPUT_MOUSE_BUTTON, probe::INPUT_DEVICE_MOUSE,
                static_cast<int64_t>(event.button.button), 1.0f, true, false);
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            queue_input_event(service, ELISA_APPLICATION_INPUT_MOUSE_BUTTON, probe::INPUT_DEVICE_MOUSE,
                static_cast<int64_t>(event.button.button), 0.0f, false, true);
            break;
        case SDL_EVENT_GAMEPAD_ADDED:
            open_gamepad(service, event.gdevice.which);
            break;
        case SDL_EVENT_GAMEPAD_REMOVED:
            close_gamepad(service, event.gdevice.which);
            break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        case SDL_EVENT_GAMEPAD_BUTTON_UP: {
            if (!has_gamepad(service, event.gbutton.which)) break;
            const int32_t code = probe::gamepad_button_code(
                static_cast<SDL_GamepadButton>(event.gbutton.button));
            if (code == 0) break;
            const bool pressed = event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
            queue_input_event(service, ELISA_APPLICATION_INPUT_GAMEPAD_BUTTON, probe::INPUT_DEVICE_GAMEPAD,
                code, pressed ? 1.0f : 0.0f, pressed, !pressed);
            break;
        }
        case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
            if (!has_gamepad(service, event.gaxis.which)) break;
            const probe::GamepadAxisInput axis = probe::gamepad_axis_input(
                static_cast<SDL_GamepadAxis>(event.gaxis.axis), event.gaxis.value);
            if (axis.negative_code != 0) {
                queue_input_event(service, ELISA_APPLICATION_INPUT_GAMEPAD_AXIS, probe::INPUT_DEVICE_GAMEPAD,
                    axis.negative_code, axis.negative_value, axis.negative_value > 0.0f,
                    axis.negative_value == 0.0f);
            }
            if (axis.positive_code != 0) {
                queue_input_event(service, ELISA_APPLICATION_INPUT_GAMEPAD_AXIS, probe::INPUT_DEVICE_GAMEPAD,
                    axis.positive_code, axis.positive_value, axis.positive_value > 0.0f,
                    axis.positive_value == 0.0f);
            }
            break;
        }
        case SDL_EVENT_WINDOW_MINIMIZED:
            service.pending_events |= ELISA_APPLICATION_EVENT_MINIMIZED;
            queue_input_event(service, ELISA_APPLICATION_INPUT_FOCUS_LOST, probe::INPUT_DEVICE_GLOBAL, 0, 0.0f, false, true);
            break;
        case SDL_EVENT_WINDOW_RESTORED:
            service.pending_events |= ELISA_APPLICATION_EVENT_RESTORED;
            break;
        default:
            break;
        }
    });
    if (!should_continue) {
        service.pending_events |= ELISA_APPLICATION_EVENT_CLOSE_REQUESTED;
        return ELISA_APPLICATION_EXIT_REQUESTED;
    }
    if (service.host.simulation_suspended()) return ELISA_APPLICATION_SUSPENDED;
    if (!service.host.run_frame()) return ELISA_APPLICATION_FRAME_FAILED;
    ++service.frame_count;
    return ELISA_APPLICATION_RUNNING;
}

#if defined(ELISA_RENDER_SCENE_TEST_PROBE)
extern "C" int32_t elisa_application_v1_test_set_minimized(int32_t minimized) {
    if (minimized != 0 && minimized != 1) return ELISA_APPLICATION_INVALID_ARGUMENT;
    ApplicationService& service = application_service();
    std::lock_guard<std::mutex> guard(service.mutex);
    if (!service.initialized) return ELISA_APPLICATION_INVALID_STATE;
    if (!on_owner_thread(service)) return ELISA_APPLICATION_WRONG_THREAD;
    SDL_Event event{};
    event.type = minimized != 0 ? SDL_EVENT_WINDOW_MINIMIZED : SDL_EVENT_WINDOW_RESTORED;
    event.window.windowID = SDL_GetWindowID(service.host.window());
    return SDL_PushEvent(&event) ? ELISA_APPLICATION_OK : ELISA_APPLICATION_FRAME_FAILED;
}
#endif

extern "C" int32_t elisa_application_v1_next_input_event(
    int32_t* kind, int32_t* device, int64_t* code, float* value,
    int32_t* pressed, int32_t* released, int32_t* chord_down) {
    if (kind == nullptr || device == nullptr || code == nullptr || value == nullptr ||
        pressed == nullptr || released == nullptr || chord_down == nullptr) {
        return ELISA_APPLICATION_INVALID_ARGUMENT;
    }
    ApplicationService& service = application_service();
    std::lock_guard<std::mutex> guard(service.mutex);
    if (!service.initialized) return ELISA_APPLICATION_INVALID_STATE;
    if (!on_owner_thread(service)) return ELISA_APPLICATION_WRONG_THREAD;
    if (service.input_event_read < service.input_event_count) {
        const QueuedInputEvent& event = service.input_events[service.input_event_read++];
        *kind = event.kind;
        *device = event.device;
        *code = event.code;
        *value = event.value;
        *pressed = event.pressed;
        *released = event.released;
        *chord_down = event.chord_down;
        return 1;
    }
    if (service.input_overflow && !service.input_overflow_reported) {
        service.input_overflow_reported = true;
        *kind = ELISA_APPLICATION_INPUT_OVERFLOW;
        *device = -1;
        *code = 0;
        *value = 0.0f;
        *pressed = 0;
        *released = 1;
        *chord_down = 0;
        return 1;
    }
    service.input_event_count = 0;
    service.input_event_read = 0;
    service.input_overflow = false;
    service.input_overflow_reported = false;
    return 0;
}

extern "C" int64_t elisa_application_v1_next_input_event_token(void) {
    ApplicationService& service = application_service();
    std::lock_guard<std::mutex> guard(service.mutex);
    if (!service.initialized) return ELISA_APPLICATION_INVALID_STATE;
    if (!on_owner_thread(service)) return ELISA_APPLICATION_WRONG_THREAD;
    int32_t kind = 0;
    int32_t device = 0;
    int64_t code = 0;
    float value = 0.0f;
    bool pressed = false;
    bool released = false;
    if (service.input_event_read < service.input_event_count) {
        const QueuedInputEvent& event = service.input_events[service.input_event_read++];
        kind = event.kind;
        device = event.device;
        code = event.code;
        value = event.value;
        pressed = event.pressed != 0;
        released = event.released != 0;
    } else if (service.input_overflow && !service.input_overflow_reported) {
        service.input_overflow_reported = true;
        kind = ELISA_APPLICATION_INPUT_OVERFLOW;
    } else {
        service.input_event_count = 0;
        service.input_event_read = 0;
        service.input_overflow = false;
        service.input_overflow_reported = false;
        return 0;
    }
    return probe::pack_input_event_token(kind, device, code, value, pressed, released);
}

extern "C" int32_t elisa_application_v1_frame_info(
    uint64_t* frame_count, uint64_t* elapsed_nanos, uint64_t* resize_serial,
    int32_t* logical_width, int32_t* logical_height, int32_t* pixel_width, int32_t* pixel_height,
    float* display_scale, uint32_t* event_flags, uint32_t* output_window_flags) {
    if (frame_count == nullptr || elapsed_nanos == nullptr || resize_serial == nullptr ||
        logical_width == nullptr || logical_height == nullptr || pixel_width == nullptr ||
        pixel_height == nullptr || display_scale == nullptr || event_flags == nullptr ||
        output_window_flags == nullptr) return ELISA_APPLICATION_INVALID_ARGUMENT;
    ApplicationService& service = application_service();
    std::lock_guard<std::mutex> guard(service.mutex);
    if (!service.initialized) return ELISA_APPLICATION_INVALID_STATE;
    if (!on_owner_thread(service)) return ELISA_APPLICATION_WRONG_THREAD;

    const probe::NativeApplication::WindowState& window = service.host.window_state();
    uint32_t window_flags = 0;
    if (window.focused) window_flags |= ELISA_APPLICATION_WINDOW_FOCUSED;
    if (window.minimized) window_flags |= ELISA_APPLICATION_WINDOW_MINIMIZED;
    if (window.fullscreen) window_flags |= ELISA_APPLICATION_WINDOW_FULLSCREEN;
    if (window.suspended()) window_flags |= ELISA_APPLICATION_WINDOW_SUSPENDED;
    if (service.host.close_requested()) window_flags |= ELISA_APPLICATION_WINDOW_CLOSE_REQUESTED;

    *frame_count = service.frame_count;
    *elapsed_nanos = service.elapsed_nanos;
    *resize_serial = window.resize_serial;
    *logical_width = window.logical_width;
    *logical_height = window.logical_height;
    *pixel_width = window.pixel_width;
    *pixel_height = window.pixel_height;
    *display_scale = window.display_scale;
    *event_flags = service.pending_events;
    *output_window_flags = window_flags;
    service.pending_events = 0;
    return ELISA_APPLICATION_OK;
}

extern "C" int32_t elisa_application_v1_request_exit(void) {
    ApplicationService& service = application_service();
    std::lock_guard<std::mutex> guard(service.mutex);
    if (!service.initialized) return ELISA_APPLICATION_INVALID_STATE;
    if (!on_owner_thread(service)) return ELISA_APPLICATION_WRONG_THREAD;
    service.host.request_close();
    service.pending_events |= ELISA_APPLICATION_EVENT_CLOSE_REQUESTED;
    return ELISA_APPLICATION_OK;
}

extern "C" int32_t elisa_application_v1_shutdown(void) {
    ApplicationService& service = application_service();
    std::lock_guard<std::mutex> guard(service.mutex);
    if (!service.initialized) return ELISA_APPLICATION_OK;
    if (!on_owner_thread(service)) return ELISA_APPLICATION_WRONG_THREAD;
    for (OpenGamepad& gamepad : service.gamepads) {
        if (gamepad.handle != nullptr) SDL_CloseGamepad(gamepad.handle);
        gamepad = OpenGamepad{};
    }
    // A runtime service may own the active path. Detach it before ordered
    // shutdown hooks release its scene and path objects.
    service.host.wicked().ActivatePath(nullptr);
    elisa_physics_v1_shutdown_from_application();
    elisa_audio_v1_shutdown_from_application();
    service.host.shutdown();
    service.initialized = false;
    service.backend_profile = {};
    service.backend_profile_valid = false;
    service.owner_thread = std::thread::id{};
    service.initialized_at = std::chrono::steady_clock::time_point{};
    service.pending_events = 0;
    service.elapsed_nanos = 0;
    return ELISA_APPLICATION_OK;
}

extern "C" uint64_t elisa_application_v1_frame_count(void) {
    ApplicationService& service = application_service();
    std::lock_guard<std::mutex> guard(service.mutex);
    return service.frame_count;
}

extern "C" uint64_t elisa_application_v1_uptime_nanos(void) {
    ApplicationService& service = application_service();
    std::lock_guard<std::mutex> guard(service.mutex);
    if (!service.initialized) return 0;
    const auto elapsed = std::chrono::steady_clock::now() - service.initialized_at;
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count());
}

extern "C" int32_t elisa_application_v1_validate_owner_thread(void) {
    ApplicationService& service = application_service();
    std::lock_guard<std::mutex> guard(service.mutex);
    if (!service.initialized) return ELISA_APPLICATION_INVALID_STATE;
    if (!on_owner_thread(service)) return ELISA_APPLICATION_WRONG_THREAD;
    return ELISA_APPLICATION_OK;
}

#include "application_capture_impl.h"
#include "application_render_probe.inc"

extern "C" int32_t elisa_application_v1_activate_render_path(void* render_path) {
    ApplicationService& service = application_service();
    std::lock_guard<std::mutex> guard(service.mutex);
    if (!service.initialized) return ELISA_APPLICATION_INVALID_STATE;
    if (!on_owner_thread(service)) return ELISA_APPLICATION_WRONG_THREAD;
    service.host.wicked().ActivatePath(static_cast<wi::RenderPath*>(render_path));
    return ELISA_APPLICATION_OK;
}

extern "C" int32_t elisa_application_v1_register_shutdown_hook(
    void* context, elisa_application_shutdown_fn function) {
    if (context == nullptr || function == nullptr) return ELISA_APPLICATION_INVALID_ARGUMENT;
    ApplicationService& service = application_service();
    std::lock_guard<std::mutex> guard(service.mutex);
    if (!service.initialized) return ELISA_APPLICATION_INVALID_STATE;
    if (!on_owner_thread(service)) return ELISA_APPLICATION_WRONG_THREAD;
    return service.host.add_shutdown_hook(context, function)
        ? ELISA_APPLICATION_OK : ELISA_APPLICATION_INITIALIZATION_FAILED;
}
