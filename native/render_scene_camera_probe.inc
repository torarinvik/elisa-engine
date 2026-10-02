extern "C" int32_t elisa_render_scene_v1_test_camera_perspective_matches(
    float width, float height, float fov, float near_clip, float far_clip) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state) || state.camera == nullptr) return 0;
    const wi::scene::CameraComponent& camera = *state.camera;
    const auto close = [](float left, float right) { return std::abs(left - right) <= 1.0e-5f; };
    return (camera._flags & wi::scene::CameraComponent::ORTHO) == 0 &&
        close(camera.width, width) && close(camera.height, height) &&
        close(camera.fov, fov) && close(camera.zNearP, near_clip) &&
        close(camera.zFarP, far_clip) ? 1 : 0;
}

extern "C" int32_t elisa_render_scene_v1_test_camera_orthographic_matches(
    float width, float height, float vertical_size) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state) || state.camera == nullptr) return 0;
    const wi::scene::CameraComponent& camera = *state.camera;
    const auto close = [](float left, float right) { return std::abs(left - right) <= 1.0e-5f; };
    return (camera._flags & wi::scene::CameraComponent::ORTHO) != 0 &&
        close(camera.width, width) && close(camera.height, height) &&
        close(camera.ortho_vertical_size, vertical_size) ? 1 : 0;
}

// A right-handed camera looking along f with up u shows f × u on the frame's
// right. Wicked's camera shows GetRight() = Up × At there. Read back into Elisa
// space, Wicked's eye, view direction and screen right must match the look-at.
extern "C" int32_t elisa_render_scene_v1_test_camera_view_matches(
    float eye_x, float eye_y, float eye_z, float target_x, float target_y, float target_z,
    float up_x, float up_y, float up_z) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state) || state.camera == nullptr) return 0;
    const XMVECTOR forward = XMVector3Normalize(XMVectorSet(target_x - eye_x,
        target_y - eye_y, target_z - eye_z, 0.0f));
    XMFLOAT3 expected_forward;
    XMFLOAT3 expected_right;
    XMStoreFloat3(&expected_forward, forward);
    XMStoreFloat3(&expected_right, XMVector3Normalize(XMVector3Cross(forward,
        XMVectorSet(up_x, up_y, up_z, 0.0f))));
    XMFLOAT3 wicked_right;
    XMStoreFloat3(&wicked_right, XMVector3Normalize(state.camera->GetRight()));
    const XMFLOAT3 eye = probe::coordinates::from_wicked(state.camera->Eye);
    const XMFLOAT3 at = probe::coordinates::from_wicked_direction(state.camera->At);
    const XMFLOAT3 right = probe::coordinates::from_wicked_direction(wicked_right);
    const auto close = [](const XMFLOAT3& left, const XMFLOAT3& right) {
        return std::abs(left.x - right.x) <= 1.0e-4f && std::abs(left.y - right.y) <= 1.0e-4f &&
            std::abs(left.z - right.z) <= 1.0e-4f;
    };
    return close(eye, XMFLOAT3(eye_x, eye_y, eye_z)) && close(at, expected_forward) &&
        close(right, expected_right) ? 1 : 0;
}

extern "C" int32_t elisa_render_scene_v1_test_camera_render_target_matches(
    int32_t width, int32_t height, int32_t expected_live) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state) || state.camera == nullptr) return 0;
    const auto& target = state.camera->render_to_texture;
    const bool live = target.resolution.x == uint32_t(width) &&
        target.resolution.y == uint32_t(height) &&
        (target.rendertarget_render.IsValid() || target.rendertarget_display.IsValid());
    return live == (expected_live != 0) ? 1 : 0;
}

extern "C" int32_t elisa_render_scene_v1_test_camera_viewport_matches(
    int32_t x, int32_t y, int32_t width, int32_t height, int32_t expected_composed_count) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state) || state.scene == nullptr) return 0;
    bool configured = false;
    for (const RenderCameraSlot& slot : state.cameras) {
        if (!slot.live || !slot.viewport_enabled || slot.viewport_x != x || slot.viewport_y != y ||
            slot.viewport_width != width || slot.viewport_height != height) continue;
        if (slot.pipeline == nullptr) continue;
        const wi::graphics::Texture& output = slot.pipeline->GetRenderResult3D();
        configured = output.IsValid() &&
            output.desc.width == uint32_t(float(width) * slot.pipeline->resolutionScale) &&
            output.desc.height == uint32_t(float(height) * slot.pipeline->resolutionScale);
        if (configured) break;
    }
    return configured && state.camera_viewports_composed == size_t(expected_composed_count) ? 1 : 0;
}

extern "C" int32_t elisa_render_scene_v1_test_camera_viewport_count_matches(int32_t expected_count) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    return state.initialized && on_owner_thread(state) && expected_count >= 0 &&
        state.camera_viewports_composed == size_t(expected_count) ? 1 : 0;
}

extern "C" int32_t elisa_render_scene_v1_test_primary_viewport_matches(
    int32_t x, int32_t y, int32_t width, int32_t height, int32_t expected_enabled) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state) || state.path == nullptr ||
        state.camera == nullptr || state.scene == nullptr) return 0;
    const bool enabled = expected_enabled != 0;
    if (state.primary_viewport.enabled != enabled) return 0;
    if (enabled && (state.primary_viewport.x != x || state.primary_viewport.y != y ||
        state.primary_viewport.width != width || state.primary_viewport.height != height)) return 0;
    const uint32_t canvas_width = enabled ? uint32_t(width) :
        (state.host_canvas_width > 0 ? state.host_canvas_width : uint32_t(state.width));
    const uint32_t canvas_height = enabled ? uint32_t(height) :
        (state.host_canvas_height > 0 ? state.host_canvas_height : uint32_t(state.height));
    const wi::graphics::Texture& output = state.path->GetRenderResult3D();
    const float scale = state.path->resolutionScale;
    const bool matches = state.path->width == canvas_width &&
        state.path->height == canvas_height && output.IsValid() &&
        output.desc.width == uint32_t(float(canvas_width) * scale) &&
        output.desc.height == uint32_t(float(canvas_height) * scale) &&
        std::abs(state.camera->width - float(canvas_width)) < 0.00001f &&
        std::abs(state.camera->height - float(canvas_height)) < 0.00001f;
    if (!matches) {
        std::fprintf(stderr, "primary viewport probe: enabled=%d requested=%d rect=%d,%d %dx%d window=%dx%d canvas=%ux%u camera=%.1fx%.1f output=%ux%u valid=%d scale=%.3f\n",
            state.primary_viewport.enabled ? 1 : 0, expected_enabled,
            state.primary_viewport.x, state.primary_viewport.y,
            state.primary_viewport.width, state.primary_viewport.height,
            state.width, state.height, state.path->width, state.path->height,
            state.camera->width, state.camera->height, output.desc.width,
            output.desc.height, output.IsValid() ? 1 : 0, scale);
    }
    return matches ? 1 : 0;
}

extern "C" int32_t elisa_render_scene_v1_test_primary_camera_visibility_mask_matches(
    uint32_t expected_mask) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state) || state.path == nullptr) return 0;
    return state.path->getLayerMask() == expected_mask ? 1 : 0;
}

extern "C" int32_t elisa_render_scene_v1_test_camera_visibility_mask_matches(
    int64_t handle, uint32_t expected_mask) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state)) return 0;
    size_t slot = MAX_RENDER_CAMERAS;
    if (!valid_camera_handle(state, handle, slot)) return 0;
    const RenderCameraSlot& camera = state.cameras[slot];
    return camera.visibility_mask == expected_mask &&
        (camera.pipeline == nullptr || camera.pipeline->getLayerMask() == expected_mask) ? 1 : 0;
}
