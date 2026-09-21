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
    state.camera->CreateOrtho(float(state.width), float(state.height),
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
    state.camera->CreatePerspective(float(state.width), float(state.height),
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
