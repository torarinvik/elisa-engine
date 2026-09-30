#include <SDL3/SDL_video.h>

// Display enumeration for the application host (included from
// application_input_events.inc). Each display is described by eight int32
// fields so the record crosses the ABI without struct layout assumptions:
// id, x, y, width, height, refresh (millihertz), content scale (permille),
// primary (0/1). Order follows SDL; the id stays stable while connected.

extern "C" int32_t elisa_application_v1_display_count(uint32_t* count) {
    if (count == nullptr) return ELISA_APPLICATION_INVALID_ARGUMENT;
    *count = 0;
    ApplicationService& service = application_service();
    std::lock_guard<std::mutex> guard(service.mutex);
    if (!service.initialized) return ELISA_APPLICATION_INVALID_STATE;
    if (!on_owner_thread(service)) return ELISA_APPLICATION_WRONG_THREAD;
    int found = 0;
    SDL_DisplayID* ids = SDL_GetDisplays(&found);
    if (ids == nullptr) return ELISA_APPLICATION_UNSUPPORTED;
    SDL_free(ids);
    *count = static_cast<uint32_t>(found);
    return ELISA_APPLICATION_OK;
}

extern "C" int32_t elisa_application_v1_display_info(uint32_t index, int32_t* fields) {
    if (fields == nullptr) return ELISA_APPLICATION_INVALID_ARGUMENT;
    for (int k = 0; k < 8; k++) fields[k] = 0;
    ApplicationService& service = application_service();
    std::lock_guard<std::mutex> guard(service.mutex);
    if (!service.initialized) return ELISA_APPLICATION_INVALID_STATE;
    if (!on_owner_thread(service)) return ELISA_APPLICATION_WRONG_THREAD;
    int found = 0;
    SDL_DisplayID* ids = SDL_GetDisplays(&found);
    if (ids == nullptr) return ELISA_APPLICATION_UNSUPPORTED;
    if (index >= static_cast<uint32_t>(found)) { SDL_free(ids); return ELISA_APPLICATION_INVALID_ARGUMENT; }
    const SDL_DisplayID id = ids[index];
    SDL_free(ids);
    SDL_Rect bounds{};
    if (!SDL_GetDisplayBounds(id, &bounds)) return ELISA_APPLICATION_UNSUPPORTED;
    const SDL_DisplayMode* mode = SDL_GetDesktopDisplayMode(id);
    const float scale = SDL_GetDisplayContentScale(id);
    fields[0] = static_cast<int32_t>(id);
    fields[1] = bounds.x;
    fields[2] = bounds.y;
    fields[3] = bounds.w;
    fields[4] = bounds.h;
    fields[5] = mode == nullptr ? 0 : static_cast<int32_t>(mode->refresh_rate * 1000.0f + 0.5f);
    fields[6] = scale <= 0.0f ? 1000 : static_cast<int32_t>(scale * 1000.0f + 0.5f);
    fields[7] = SDL_GetPrimaryDisplay() == id ? 1 : 0;
    return ELISA_APPLICATION_OK;
}

// Centres the window on the display with `id`. INVALID_ARGUMENT when that
// display is not connected.
extern "C" int32_t elisa_application_v1_move_window_to_display(int32_t id) {
    ApplicationService& service = application_service();
    std::lock_guard<std::mutex> guard(service.mutex);
    if (!service.initialized || service.host.window() == nullptr) return ELISA_APPLICATION_INVALID_STATE;
    if (!on_owner_thread(service)) return ELISA_APPLICATION_WRONG_THREAD;
    SDL_Rect bounds{};
    if (id <= 0 || !SDL_GetDisplayBounds(static_cast<SDL_DisplayID>(id), &bounds)) {
        return ELISA_APPLICATION_INVALID_ARGUMENT;
    }
    const int position = static_cast<int>(SDL_WINDOWPOS_CENTERED_DISPLAY(static_cast<SDL_DisplayID>(id)));
    return SDL_SetWindowPosition(service.host.window(), position, position) ? ELISA_APPLICATION_OK
                                                                           : ELISA_APPLICATION_UNSUPPORTED;
}

// The id of the display that holds the window, or 0 when unknown.
extern "C" int32_t elisa_application_v1_window_display(int32_t* id) {
    if (id == nullptr) return ELISA_APPLICATION_INVALID_ARGUMENT;
    *id = 0;
    ApplicationService& service = application_service();
    std::lock_guard<std::mutex> guard(service.mutex);
    if (!service.initialized || service.host.window() == nullptr) return ELISA_APPLICATION_INVALID_STATE;
    if (!on_owner_thread(service)) return ELISA_APPLICATION_WRONG_THREAD;
    *id = static_cast<int32_t>(SDL_GetDisplayForWindow(service.host.window()));
    return ELISA_APPLICATION_OK;
}
