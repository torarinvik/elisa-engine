#include "application_abi.h"
#include "native_application.h"
#include "wiHelper.h"
#include "wiRenderer.h"

#include <cstddef>
#include <chrono>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

namespace {

constexpr size_t INPUT_EVENT_CAPACITY = 512;

struct QueuedInputEvent {
    int32_t kind = 0;
    int32_t device = 0;
    int64_t code = 0;
    float value = 0.0f;
    int32_t pressed = 0;
    int32_t released = 0;
    int32_t chord_down = 0;
};

struct ApplicationService {
    std::mutex mutex;
    probe::NativeApplication host;
    std::thread::id owner_thread{};
    std::chrono::steady_clock::time_point previous_pump{};
    uint64_t frame_count = 0;
    uint64_t elapsed_nanos = 0;
    uint32_t pending_events = 0;
    std::array<QueuedInputEvent, INPUT_EVENT_CAPACITY> input_events{};
    size_t input_event_count = 0;
    size_t input_event_read = 0;
    bool input_overflow = false;
    bool input_overflow_reported = false;
    bool initialized = false;
};

void queue_input_event(ApplicationService& service, int32_t kind, int32_t device,
    int64_t code, float value, bool pressed, bool released) {
    if (service.input_event_count == INPUT_EVENT_CAPACITY) {
        service.input_overflow = true;
        return;
    }
    service.input_events[service.input_event_count++] = QueuedInputEvent{
        kind, device, code, value, pressed ? 1 : 0, released ? 1 : 0, 0};
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
    if (configured == nullptr || configured[0] == '\0' || std::strlen(configured) > 4096) return;
    std::string path(configured);
    if (path.back() != '/') path.push_back('/');
    wi::renderer::SetShaderPath(path);
    wi::renderer::SetShaderSourcePath(path);
}

} // namespace

extern "C" uint32_t elisa_application_abi_version(void) {
    return ELISA_APPLICATION_ABI_VERSION;
}

extern "C" int32_t elisa_application_v1_initialize(
    const char* title, int32_t width, int32_t height, int32_t hidden) {
    if (!valid_title(title) || width <= 0 || height <= 0 || width > 16384 || height > 16384 ||
        (hidden != 0 && hidden != 1)) {
        return ELISA_APPLICATION_INVALID_ARGUMENT;
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

    service.owner_thread = std::this_thread::get_id();
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
            queue_input_event(service, ELISA_APPLICATION_INPUT_FOCUS_LOST, -1, 0, 0.0f, false, true);
            break;
        case SDL_EVENT_KEY_DOWN:
            queue_input_event(service, ELISA_APPLICATION_INPUT_KEY, 0,
                static_cast<int64_t>(event.key.key), 1.0f, true, false);
            break;
        case SDL_EVENT_KEY_UP:
            queue_input_event(service, ELISA_APPLICATION_INPUT_KEY, 0,
                static_cast<int64_t>(event.key.key), 0.0f, false, true);
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            queue_input_event(service, ELISA_APPLICATION_INPUT_MOUSE_BUTTON, 1,
                static_cast<int64_t>(event.button.button), 1.0f, true, false);
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            queue_input_event(service, ELISA_APPLICATION_INPUT_MOUSE_BUTTON, 1,
                static_cast<int64_t>(event.button.button), 0.0f, false, true);
            break;
        case SDL_EVENT_WINDOW_MINIMIZED:
            service.pending_events |= ELISA_APPLICATION_EVENT_MINIMIZED;
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
    // A runtime service may own the active path. Detach it before ordered
    // shutdown hooks release its scene and path objects.
    service.host.wicked().ActivatePath(nullptr);
    service.host.shutdown();
    service.initialized = false;
    service.owner_thread = std::thread::id{};
    service.pending_events = 0;
    service.elapsed_nanos = 0;
    return ELISA_APPLICATION_OK;
}

extern "C" uint64_t elisa_application_v1_frame_count(void) {
    ApplicationService& service = application_service();
    std::lock_guard<std::mutex> guard(service.mutex);
    return service.frame_count;
}

extern "C" int32_t elisa_application_v1_validate_owner_thread(void) {
    ApplicationService& service = application_service();
    std::lock_guard<std::mutex> guard(service.mutex);
    if (!service.initialized) return ELISA_APPLICATION_INVALID_STATE;
    if (!on_owner_thread(service)) return ELISA_APPLICATION_WRONG_THREAD;
    return ELISA_APPLICATION_OK;
}

extern "C" int32_t elisa_application_v1_active_path_center_differs_from_corner(void* render_path) {
    ApplicationService& service = application_service();
    std::lock_guard<std::mutex> guard(service.mutex);
    if (!service.initialized || !on_owner_thread(service) || render_path == nullptr ||
        service.host.wicked().GetActivePath() != static_cast<wi::RenderPath*>(render_path) ||
        wi::graphics::GetDevice() == nullptr) {
        return 0;
    }
    wi::graphics::GetDevice()->WaitForGPU();
    const wi::graphics::Texture frame = wi::graphics::GetDevice()->GetBackBuffer(
        &service.host.wicked().swapChain);
    const wi::graphics::TextureDesc& desc = frame.GetDesc();
    wi::vector<uint8_t> rgba;
    if (!frame.IsValid() || desc.width == 0 || desc.height == 0 ||
        wi::graphics::GetFormatStride(desc.format) != sizeof(uint32_t) ||
        !wi::helper::saveTextureToMemoryFile(frame, "RAW", rgba) ||
        rgba.size() < size_t(desc.width) * desc.height * sizeof(uint32_t)) {
        return 0;
    }
    const size_t center_index = (size_t(desc.height / 2) * desc.width + desc.width / 2) * sizeof(uint32_t);
    const uint32_t corner = uint32_t(rgba[0]) | (uint32_t(rgba[1]) << 8) |
        (uint32_t(rgba[2]) << 16) | (uint32_t(rgba[3]) << 24);
    const uint32_t center = uint32_t(rgba[center_index]) | (uint32_t(rgba[center_index + 1]) << 8) |
        (uint32_t(rgba[center_index + 2]) << 16) | (uint32_t(rgba[center_index + 3]) << 24);
    if (center == corner) {
        std::fprintf(stderr, "render evidence: center/corner pixels both 0x%08x in %ux%u frame\n",
            center, desc.width, desc.height);
        return 0;
    }
    return 1;
}

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
