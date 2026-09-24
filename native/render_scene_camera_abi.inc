extern "C" int32_t elisa_render_scene_v1_resize(int32_t width, int32_t height) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    return resize_unlocked(state, width, height);
}

extern "C" int32_t elisa_render_scene_v1_set_camera_render_target(
    int32_t width, int32_t height, float update_interval) {
    if (!valid_viewport(width, height) || !finite(update_interval) ||
        update_interval < 0.0f || update_interval > 60.0f) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    if (state.camera == nullptr) return ELISA_RENDER_SCENE_BACKEND_FAILED;
    state.camera->render_to_texture.resolution = XMUINT2(uint32_t(width), uint32_t(height));
    state.camera->render_to_texture.sample_count = 1;
    state.camera->render_to_texture.update_interval = update_interval;
    state.camera->render_to_texture.time_accumulator = 0.0f;
    return ELISA_RENDER_SCENE_OK;
}

extern "C" int32_t elisa_render_scene_v1_clear_camera_render_target(void) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    if (state.camera == nullptr) return ELISA_RENDER_SCENE_BACKEND_FAILED;
    state.camera->render_to_texture = {};
    return ELISA_RENDER_SCENE_OK;
}

extern "C" int32_t elisa_render_scene_v1_set_camera_orthographic_height(float vertical_size) {
    if (!finite(vertical_size) || vertical_size < MIN_ORTHOGRAPHIC_HEIGHT ||
        vertical_size > MAX_ORTHOGRAPHIC_HEIGHT) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    if (state.camera == nullptr) return ELISA_RENDER_SCENE_BACKEND_FAILED;
    state.vertical_size = vertical_size;
    state.perspective_camera = false;
    state.camera->CreateOrtho(float(primary_view_width(state)), float(primary_view_height(state)),
        state.camera_near_clip, state.camera_far_clip, vertical_size);
    return ELISA_RENDER_SCENE_OK;
}

extern "C" int32_t elisa_render_scene_v1_set_camera_perspective(
    float vertical_fov_radians, float near_clip, float far_clip) {
    if (!finite(vertical_fov_radians) || vertical_fov_radians <= MIN_CAMERA_FOV_RADIANS ||
        vertical_fov_radians >= MAX_CAMERA_FOV_RADIANS || !finite(near_clip) ||
        near_clip < MIN_CAMERA_CLIP_DISTANCE || !bounded(far_clip) || far_clip <= near_clip) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    if (state.camera == nullptr) return ELISA_RENDER_SCENE_BACKEND_FAILED;
    state.perspective_fov = vertical_fov_radians;
    state.camera_near_clip = near_clip;
    state.camera_far_clip = far_clip;
    state.perspective_camera = true;
    state.camera->CreatePerspective(float(primary_view_width(state)), float(primary_view_height(state)),
        near_clip, far_clip, vertical_fov_radians);
    return ELISA_RENDER_SCENE_OK;
}

extern "C" int32_t elisa_render_scene_v1_set_camera_look_at(
    float eye_x, float eye_y, float eye_z,
    float target_x, float target_y, float target_z,
    float up_x, float up_y, float up_z) {
    if (!valid_look_at(eye_x, eye_y, eye_z, target_x, target_y, target_z, up_x, up_y, up_z)) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    state.eye[0] = eye_x; state.eye[1] = eye_y; state.eye[2] = eye_z;
    state.target[0] = target_x; state.target[1] = target_y; state.target[2] = target_z;
    state.up[0] = up_x; state.up[1] = up_y; state.up[2] = up_z;
    apply_camera_look_at(state);
    return ELISA_RENDER_SCENE_OK;
}

extern "C" int32_t elisa_render_scene_v1_camera_ray(
    float screen_x, float screen_y,
    float* origin_x, float* origin_y, float* origin_z,
    float* direction_x, float* direction_y, float* direction_z) {
    if (origin_x == nullptr || origin_y == nullptr || origin_z == nullptr ||
        direction_x == nullptr || direction_y == nullptr || direction_z == nullptr ||
        !finite(screen_x) || !finite(screen_y)) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    if (state.camera == nullptr || state.width <= 0 || state.height <= 0) {
        return ELISA_RENDER_SCENE_BACKEND_FAILED;
    }
    const float viewport_x = state.primary_viewport.enabled ?
        float(state.primary_viewport.x) : 0.0f;
    const float viewport_y = state.primary_viewport.enabled ?
        float(state.primary_viewport.y) : 0.0f;
    const float viewport_width = float(primary_view_width(state));
    const float viewport_height = float(primary_view_height(state));
    if (screen_x < viewport_x || screen_y < viewport_y ||
        screen_x > viewport_x + viewport_width ||
        screen_y > viewport_y + viewport_height) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    const float ndc_x = ((screen_x - viewport_x) / viewport_width) * 2.0f - 1.0f;
    const float ndc_y = 1.0f - ((screen_y - viewport_y) / viewport_height) * 2.0f;
    const XMMATRIX inverse_view_projection = state.camera->GetInvViewProjection();
    const XMVECTOR near_point = XMVector3TransformCoord(
        XMVectorSet(ndc_x, ndc_y, 1.0f, 1.0f), inverse_view_projection);
    const XMVECTOR far_point = XMVector3TransformCoord(
        XMVectorSet(ndc_x, ndc_y, 0.0f, 1.0f), inverse_view_projection);
    const XMVECTOR direction = XMVector3Normalize(XMVectorSubtract(far_point, near_point));
    XMFLOAT3 wicked_origin;
    XMFLOAT3 wicked_direction;
    if (state.camera->IsOrtho()) {
        XMStoreFloat3(&wicked_origin, near_point);
    } else {
        wicked_origin = state.camera->Eye;
    }
    XMStoreFloat3(&wicked_direction, direction);
    const XMFLOAT3 authored_origin = probe::coordinates::from_wicked(wicked_origin);
    const XMFLOAT3 authored_direction = probe::coordinates::from_wicked_direction(wicked_direction);
    if (!bounded(authored_origin.x) || !bounded(authored_origin.y) || !bounded(authored_origin.z) ||
        !bounded(authored_direction.x) || !bounded(authored_direction.y) ||
        !bounded(authored_direction.z)) {
        return ELISA_RENDER_SCENE_BACKEND_FAILED;
    }
    *origin_x = authored_origin.x;
    *origin_y = authored_origin.y;
    *origin_z = authored_origin.z;
    *direction_x = authored_direction.x;
    *direction_y = authored_direction.y;
    *direction_z = authored_direction.z;
    return ELISA_RENDER_SCENE_OK;
}
