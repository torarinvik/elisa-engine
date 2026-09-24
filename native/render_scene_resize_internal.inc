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

    if (state.perspective_camera) {
        state.camera->CreatePerspective(float(width), float(height),
            state.camera_near_clip, state.camera_far_clip, state.perspective_fov);
    } else {
        state.camera->CreateOrtho(float(width), float(height),
            state.camera_near_clip, state.camera_far_clip, state.vertical_size);
    }
    apply_camera_look_at(state);
    for (size_t index = 0; index < viewport_resize_count; ++index) {
        CameraViewportResize& resized = viewport_resizes[index];
        resized.slot->viewport_x = resized.x;
        resized.slot->viewport_y = resized.y;
        resized.slot->viewport_width = resized.width;
        resized.slot->viewport_height = resized.height;
        resized.camera->render_to_texture.resolution =
            XMUINT2(uint32_t(resized.width), uint32_t(resized.height));
        if (resized.camera->IsOrtho()) {
            resized.camera->CreateOrtho(float(resized.width), float(resized.height),
                resized.camera->zNearP, resized.camera->zFarP,
                resized.camera->ortho_vertical_size);
        } else {
            resized.camera->CreatePerspective(float(resized.width), float(resized.height),
                resized.camera->zNearP, resized.camera->zFarP, resized.camera->fov);
        }
    }
    state.width = width;
    state.height = height;
    return ELISA_RENDER_SCENE_OK;
}
