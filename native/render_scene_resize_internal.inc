int32_t primary_view_width(const RenderSceneService& state) {
    return state.primary_viewport.enabled ? state.primary_viewport.width : state.width;
}

int32_t primary_view_height(const RenderSceneService& state) {
    return state.primary_viewport.enabled ? state.primary_viewport.height : state.height;
}

void sync_primary_path_canvas(RenderSceneService& state) {
    if (state.path == nullptr) return;
    state.path->width = uint32_t(primary_view_width(state));
    state.path->height = uint32_t(primary_view_height(state));
}

void sync_active_camera_projection(RenderSceneService& state) {
    if (state.camera == nullptr) return;
    const float width = float(primary_view_width(state));
    const float height = float(primary_view_height(state));
    if (state.perspective_camera) {
        state.camera->CreatePerspective(width, height, state.camera_near_clip,
            state.camera_far_clip, state.perspective_fov);
    } else {
        state.camera->CreateOrtho(width, height, state.camera_near_clip,
            state.camera_far_clip, state.vertical_size);
    }
}

int32_t resize_unlocked(RenderSceneService& state, int32_t width, int32_t height) {
    if (!valid_viewport(width, height)) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    if (state.camera == nullptr) return ELISA_RENDER_SCENE_BACKEND_FAILED;
    if (state.width <= 0 || state.height <= 0 || state.scene == nullptr) {
        return ELISA_RENDER_SCENE_BACKEND_FAILED;
    }

    struct CameraViewportResize {
        RenderCameraSlot* slot;
        wi::scene::CameraComponent* camera;
        int32_t x, y, width, height;
    };
    std::array<CameraViewportResize, MAX_RENDER_CAMERAS> viewport_resizes{};
    size_t viewport_resize_count = 0;
    const auto scale_axis = [](int32_t start, int32_t extent, int32_t previous,
        int32_t next, int32_t& scaled_start, int32_t& scaled_extent) {
        const int64_t left = int64_t(start) * next / previous;
        const int64_t end = int64_t(start + extent) * next;
        const int64_t right = (end + previous - 1) / previous;
        scaled_start = int32_t(std::min<int64_t>(left, next - 1));
        const int64_t bounded_right = std::min<int64_t>(std::max(right,
            int64_t(scaled_start) + 1), next);
        scaled_extent = int32_t(bounded_right - scaled_start);
    };
    int32_t primary_x = 0;
    int32_t primary_y = 0;
    int32_t primary_width = width;
    int32_t primary_height = height;
    if (state.primary_viewport.enabled) {
        scale_axis(state.primary_viewport.x, state.primary_viewport.width,
            state.width, width, primary_x, primary_width);
        scale_axis(state.primary_viewport.y, state.primary_viewport.height,
            state.height, height, primary_y, primary_height);
    }
    for (RenderCameraSlot& slot : state.cameras) {
        if (!slot.live || !slot.viewport_enabled) continue;
        wi::scene::CameraComponent* camera = state.scene->cameras.GetComponent(slot.entity);
        if (camera == nullptr) return ELISA_RENDER_SCENE_BACKEND_FAILED;
        CameraViewportResize& resized = viewport_resizes[viewport_resize_count++];
        resized.slot = &slot;
        resized.camera = camera;
        scale_axis(slot.viewport_x, slot.viewport_width, state.width, width,
            resized.x, resized.width);
        scale_axis(slot.viewport_y, slot.viewport_height, state.height, height,
            resized.y, resized.height);
    }

    state.primary_viewport.x = primary_x;
    state.primary_viewport.y = primary_y;
    state.primary_viewport.width = primary_width;
    state.primary_viewport.height = primary_height;
    state.width = width;
    state.height = height;
    sync_primary_path_canvas(state);
    sync_active_camera_projection(state);
    apply_camera_look_at(state);
    for (size_t index = 0; index < viewport_resize_count; ++index) {
        CameraViewportResize& resized = viewport_resizes[index];
        resized.slot->viewport_x = resized.x;
        resized.slot->viewport_y = resized.y;
        resized.slot->viewport_width = resized.width;
        resized.slot->viewport_height = resized.height;
        if (resized.slot->pipeline != nullptr) {
            resized.slot->pipeline->init(uint32_t(resized.width), uint32_t(resized.height),
                state.path->dpi);
            resized.slot->pipeline->scaling = state.path->scaling;
            resized.slot->render_pending = true;
            resized.slot->rendered = false;
            resized.slot->update_elapsed = 0.0f;
        }
        if (resized.camera->IsOrtho()) {
            resized.camera->CreateOrtho(float(resized.width), float(resized.height),
                resized.camera->zNearP, resized.camera->zFarP,
                resized.camera->ortho_vertical_size);
        } else {
            resized.camera->CreatePerspective(float(resized.width), float(resized.height),
                resized.camera->zNearP, resized.camera->zFarP, resized.camera->fov);
        }
    }
    return ELISA_RENDER_SCENE_OK;
}

extern "C" int32_t elisa_render_scene_v1_set_primary_viewport(
    int32_t x, int32_t y, int32_t width, int32_t height) {
    if (x < 0 || y < 0 || !valid_viewport(width, height)) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    if (width > state.width || height > state.height || x > state.width - width ||
        y > state.height - height) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    state.primary_viewport = PrimaryViewportState{x, y, width, height, true};
    sync_primary_path_canvas(state);
    sync_active_camera_projection(state);
    return ELISA_RENDER_SCENE_OK;
}

extern "C" int32_t elisa_render_scene_v1_clear_primary_viewport(void) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    state.primary_viewport = {};
    sync_primary_path_canvas(state);
    sync_active_camera_projection(state);
    return ELISA_RENDER_SCENE_OK;
}
