extern "C" int32_t elisa_render_scene_v1_initialize(
    int32_t width, int32_t height, float vertical_size) {
    if (!valid_viewport(width, height) || !finite(vertical_size) ||
        vertical_size < MIN_ORTHOGRAPHIC_HEIGHT || vertical_size > MAX_ORTHOGRAPHIC_HEIGHT) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    const int32_t application_status = elisa_application_v1_validate_owner_thread();
    if (application_status == ELISA_APPLICATION_INVALID_STATE) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (application_status == ELISA_APPLICATION_WRONG_THREAD) return ELISA_RENDER_SCENE_WRONG_THREAD;
    if (application_status != ELISA_APPLICATION_OK) return ELISA_RENDER_SCENE_BACKEND_FAILED;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (state.initialized) return ELISA_RENDER_SCENE_ALREADY_INITIALIZED;
    if (wi::graphics::GetDevice() == nullptr) return ELISA_RENDER_SCENE_NOT_INITIALIZED;

    try {
        state.scene = std::make_unique<wi::scene::Scene>();
        state.path = std::make_unique<ElisaRenderPath3D>(&state.electric_arcs);
    } catch (...) {
        reset_unlocked(state);
        return ELISA_RENDER_SCENE_BACKEND_FAILED;
    }

    state.owner_thread = std::this_thread::get_id();
    state.vertical_size = vertical_size;
    state.perspective_fov = DEFAULT_CAMERA_FOV_RADIANS;
    state.camera_near_clip = DEFAULT_CAMERA_NEAR_CLIP;
    state.camera_far_clip = DEFAULT_CAMERA_FAR_CLIP;
    state.perspective_camera = false;
    state.camera_entity = state.scene->Entity_CreateCamera("elisa_runtime_camera", float(width), float(height));
    state.camera = state.scene->cameras.GetComponent(state.camera_entity);
    wi::scene::TransformComponent* camera_transform = state.scene->transforms.GetComponent(state.camera_entity);
    if (state.camera_entity == wi::ecs::INVALID_ENTITY || state.camera == nullptr || camera_transform == nullptr) {
        reset_unlocked(state);
        return ELISA_RENDER_SCENE_BACKEND_FAILED;
    }

    state.camera->CreateOrtho(float(width), float(height),
        state.camera_near_clip, state.camera_far_clip, vertical_size);
    state.eye[0] = 0.0f;
    state.eye[1] = vertical_size * 1.5f;
    state.eye[2] = 0.0f;
    state.target[0] = state.target[1] = state.target[2] = 0.0f;
    state.up[0] = 0.0f;
    state.up[1] = 0.0f;
    state.up[2] = -1.0f;
    apply_camera_look_at(state);
    state.path->scene = state.scene.get();
    state.path->camera = state.camera;
    state.path->setOcclusionCullingEnabled(false);

    if (!state.shutdown_hook_registered) {
        if (elisa_application_v1_register_shutdown_hook(&state, on_application_shutdown) != ELISA_APPLICATION_OK) {
            reset_unlocked(state);
            return ELISA_RENDER_SCENE_BACKEND_FAILED;
        }
        state.shutdown_hook_registered = true;
    }
    state.initialized = true;
    if (elisa_application_v1_activate_render_path(state.path.get()) != ELISA_APPLICATION_OK) {
        reset_unlocked(state);
        return ELISA_RENDER_SCENE_BACKEND_FAILED;
    }
    state.width = width;
    state.height = height;
    return ELISA_RENDER_SCENE_OK;
}
