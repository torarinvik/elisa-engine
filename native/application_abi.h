#pragma once

// Versioned, vendor-free lifecycle ABI for Elisa-authored applications.
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    ELISA_APPLICATION_ABI_VERSION = 1u,
    ELISA_APPLICATION_OK = 0,
    ELISA_APPLICATION_RUNNING = 1,
    ELISA_APPLICATION_SUSPENDED = 2,
    ELISA_APPLICATION_EXIT_REQUESTED = 3,
    ELISA_APPLICATION_INVALID_ARGUMENT = -1,
    ELISA_APPLICATION_INVALID_STATE = -2,
    ELISA_APPLICATION_INITIALIZATION_FAILED = -3,
    ELISA_APPLICATION_WRONG_THREAD = -4,
    ELISA_APPLICATION_FRAME_FAILED = -5,
    ELISA_APPLICATION_PROFILE_UNAVAILABLE = -6,
    ELISA_APPLICATION_SHADER_PATH_INVALID = -7,
};

// Engine-internal extension points for runtime services. The public Elisa
// surface uses opaque handles and scalar values; these callbacks stay in the
// native engine boundary.
typedef void (*elisa_application_shutdown_fn)(void* context);

enum {
    ELISA_APPLICATION_EVENT_CLOSE_REQUESTED = 1u << 0,
    ELISA_APPLICATION_EVENT_RESIZED = 1u << 1,
    ELISA_APPLICATION_EVENT_FOCUS_GAINED = 1u << 2,
    ELISA_APPLICATION_EVENT_FOCUS_LOST = 1u << 3,
    ELISA_APPLICATION_EVENT_MINIMIZED = 1u << 4,
    ELISA_APPLICATION_EVENT_RESTORED = 1u << 5,
};

enum {
    ELISA_APPLICATION_PROVIDER_JOLT_PHYSICS = 1,
    ELISA_APPLICATION_PROVIDER_MINIAUDIO_SILENT = 2,
    ELISA_APPLICATION_PROVIDER_MINIAUDIO_DEFAULT = 3,
};

enum {
    ELISA_APPLICATION_WINDOW_FOCUSED = 1u << 0,
    ELISA_APPLICATION_WINDOW_MINIMIZED = 1u << 1,
    ELISA_APPLICATION_WINDOW_FULLSCREEN = 1u << 2,
    ELISA_APPLICATION_WINDOW_SUSPENDED = 1u << 3,
    ELISA_APPLICATION_WINDOW_CLOSE_REQUESTED = 1u << 4,
};

enum {
    ELISA_APPLICATION_INPUT_KEY = 1,
    ELISA_APPLICATION_INPUT_MOUSE_BUTTON = 2,
    ELISA_APPLICATION_INPUT_FOCUS_LOST = 3,
    ELISA_APPLICATION_INPUT_OVERFLOW = 4,
    ELISA_APPLICATION_INPUT_GAMEPAD_BUTTON = 5,
    ELISA_APPLICATION_INPUT_GAMEPAD_AXIS = 6,
    ELISA_APPLICATION_INPUT_GAMEPAD_CONNECTED = 7,
    ELISA_APPLICATION_INPUT_GAMEPAD_DISCONNECTED = 8,
};

uint32_t elisa_application_abi_version(void);
const char* elisa_application_v1_project_title(void);
int32_t elisa_application_v1_project_width(void);
int32_t elisa_application_v1_project_height(void);
int32_t elisa_application_v1_project_hidden(void);
int32_t elisa_application_v1_initialize(const char* title, int32_t width, int32_t height, int32_t hidden);
// Returns the validated startup-time native service and resource profile.
int32_t elisa_application_v1_backend_profile(
    int64_t* capabilities, int64_t* optional, int64_t* max_viewports,
    int64_t* max_workers, int64_t* memory_budget, int64_t* memory_usage,
    int64_t* formats, int64_t* graphics_workers, int64_t* streaming_workers,
    int64_t* service_version);
// Performs a bounded real initialization/teardown probe for an engine-owned
// fallback adapter. Returns 1 when usable, 0 when unavailable, or an app error.
int32_t elisa_application_v1_fallback_provider_available(int32_t provider);
int32_t elisa_application_v1_pump(void);
// One typed scalar-output call avoids compiler-specific aggregate layout.
// Event flags are coalesced edges since the previous successful read; window
// flags describe current levels.
int32_t elisa_application_v1_frame_info(
    uint64_t* frame_count, uint64_t* elapsed_nanos, uint64_t* resize_serial,
    int32_t* logical_width, int32_t* logical_height, int32_t* pixel_width, int32_t* pixel_height,
    float* display_scale, uint32_t* event_flags, uint32_t* window_flags);
// Returns 1 when an input event was written, 0 when the queue is empty, or a
// negative application status on failure. Events are drained in SDL order.
int32_t elisa_application_v1_next_input_event(
    int32_t* kind, int32_t* device, int64_t* code, float* value,
    int32_t* pressed, int32_t* released, int32_t* chord_down);
// Compact stage1-compatible path: digital events retain their 32-bit code;
// gamepad axis events carry a 12-bit portable code and normalized 20-bit value.
// Bits 38 and 39 carry the pressed/released edges for every event.
int64_t elisa_application_v1_next_input_event_token(void);
int32_t elisa_application_v1_request_exit(void);
int32_t elisa_application_v1_shutdown(void);
uint64_t elisa_application_v1_frame_count(void);
// Monotonic nanoseconds since the current application host initialized.
// Returns zero while the host is stopped.
uint64_t elisa_application_v1_uptime_nanos(void);
int32_t elisa_application_v1_activate_render_path(void* render_path);
int32_t elisa_application_v1_register_shutdown_hook(
    void* context, elisa_application_shutdown_fn function);
int32_t elisa_application_v1_validate_owner_thread(void);
int32_t elisa_application_v1_active_path_center_differs_from_corner(void* render_path);

#ifdef __cplusplus
}
#endif
