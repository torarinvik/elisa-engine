// Generation-checked secondary cameras for editor, split-view, and game views.
namespace {

size_t find_free_camera_slot(const RenderSceneService& state) {
    for (size_t index = 0; index < MAX_RENDER_CAMERAS; ++index) {
        if (!state.cameras[index].live) return index;
    }
    return MAX_RENDER_CAMERAS;
}

uint64_t encode_camera_handle(size_t slot, uint64_t generation) {
    return (generation << CAMERA_HANDLE_SLOT_BITS) | uint64_t(slot + 1);
}

size_t decode_camera_handle(int64_t value, uint64_t& generation) {
    if (value <= 0) return MAX_RENDER_CAMERAS;
    const uint64_t encoded = uint64_t(value);
    const uint64_t slot = encoded & CAMERA_HANDLE_SLOT_MASK;
    if (slot == 0 || slot > MAX_RENDER_CAMERAS) return MAX_RENDER_CAMERAS;
    generation = encoded >> CAMERA_HANDLE_SLOT_BITS;
    return size_t(slot - 1);
}

bool valid_camera_descriptor(int32_t projection, int32_t width, int32_t height,
    float projection_value, float near_clip, float far_clip) {
    if ((projection != 0 && projection != 1) || !valid_viewport(width, height) ||
        !finite(projection_value) || !finite(near_clip) || !bounded(far_clip) ||
        near_clip < MIN_CAMERA_CLIP_DISTANCE || far_clip <= near_clip) return false;
    if (projection == 0) {
        return projection_value >= MIN_ORTHOGRAPHIC_HEIGHT &&
            projection_value <= MAX_ORTHOGRAPHIC_HEIGHT;
    }
    return projection_value > MIN_CAMERA_FOV_RADIANS &&
        projection_value < MAX_CAMERA_FOV_RADIANS;
}

bool configure_camera(wi::scene::CameraComponent& camera, int32_t projection,
    int32_t width, int32_t height, float projection_value, float near_clip, float far_clip) {
    if (!valid_camera_descriptor(projection, width, height, projection_value, near_clip, far_clip)) {
        return false;
    }
    if (projection == 0) {
        camera.CreateOrtho(float(width), float(height), near_clip, far_clip, projection_value);
    } else {
        camera.CreatePerspective(float(width), float(height), near_clip, far_clip, projection_value);
    }
    return true;
}

bool valid_camera_handle(const RenderSceneService& state, int64_t value, size_t& slot) {
    uint64_t generation = 0;
    slot = decode_camera_handle(value, generation);
    if (slot >= MAX_RENDER_CAMERAS || generation == 0) return false;
    const RenderCameraSlot& camera = state.cameras[slot];
    return camera.live && camera.generation == generation &&
        camera.entity != wi::ecs::INVALID_ENTITY;
}

void sync_camera_settings(RenderSceneService& state, const wi::scene::CameraComponent& camera) {
    state.perspective_camera = !camera.IsOrtho();
    state.perspective_fov = camera.fov;
    state.camera_near_clip = camera.zNearP;
    state.camera_far_clip = camera.zFarP;
    state.vertical_size = camera.ortho_vertical_size;
}

void refresh_active_camera(RenderSceneService& state) {
    state.camera = state.scene->cameras.GetComponent(state.camera_entity);
    if (state.path != nullptr) state.path->camera = state.camera;
}

} // namespace

extern "C" int64_t elisa_render_scene_v1_create_camera(
    int32_t projection, int32_t width, int32_t height,
    float projection_value, float near_clip, float far_clip) {
    if (!valid_camera_descriptor(projection, width, height, projection_value, near_clip, far_clip)) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    const size_t slot = find_free_camera_slot(state);
    if (slot == MAX_RENDER_CAMERAS) return ELISA_RENDER_SCENE_CAPACITY;
    RenderCameraSlot& camera_slot = state.cameras[slot];
    if (camera_slot.generation >= MAX_GENERATION) return ELISA_RENDER_SCENE_GENERATION_EXHAUSTED;
    wi::ecs::Entity entity = wi::ecs::INVALID_ENTITY;
    try {
        entity = state.scene->Entity_CreateCamera("elisa_camera_" + std::to_string(slot),
            float(width), float(height));
    } catch (...) {
        return ELISA_RENDER_SCENE_BACKEND_FAILED;
    }
    wi::scene::CameraComponent* camera = state.scene->cameras.GetComponent(entity);
    if (entity == wi::ecs::INVALID_ENTITY || camera == nullptr ||
        !configure_camera(*camera, projection, width, height, projection_value, near_clip, far_clip)) {
        if (entity != wi::ecs::INVALID_ENTITY) state.scene->Entity_Remove(entity);
        return ELISA_RENDER_SCENE_BACKEND_FAILED;
    }
    camera_slot.entity = entity;
    camera_slot.generation += 1;
    camera_slot.live = true;
    refresh_active_camera(state);
    return int64_t(encode_camera_handle(slot, camera_slot.generation));
}

extern "C" int32_t elisa_render_scene_v1_update_camera(
    int64_t handle, int32_t projection, int32_t width, int32_t height,
    float projection_value, float near_clip, float far_clip) {
    if (!valid_camera_descriptor(projection, width, height, projection_value, near_clip, far_clip)) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    size_t slot = MAX_RENDER_CAMERAS;
    if (!valid_camera_handle(state, handle, slot)) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    wi::scene::CameraComponent* camera = state.scene->cameras.GetComponent(state.cameras[slot].entity);
    if (camera == nullptr || !configure_camera(*camera, projection, width, height,
        projection_value, near_clip, far_clip)) return ELISA_RENDER_SCENE_BACKEND_FAILED;
    if (state.camera_entity == state.cameras[slot].entity) sync_camera_settings(state, *camera);
    return ELISA_RENDER_SCENE_OK;
}

extern "C" int32_t elisa_render_scene_v1_activate_camera(int64_t handle) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    size_t slot = MAX_RENDER_CAMERAS;
    if (!valid_camera_handle(state, handle, slot)) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    wi::scene::CameraComponent* camera = state.scene->cameras.GetComponent(state.cameras[slot].entity);
    if (camera == nullptr) return ELISA_RENDER_SCENE_BACKEND_FAILED;
    state.camera_entity = state.cameras[slot].entity;
    state.camera = camera;
    state.path->camera = camera;
    sync_camera_settings(state, *camera);
    return ELISA_RENDER_SCENE_OK;
}

extern "C" int32_t elisa_render_scene_v1_activate_default_camera(void) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    wi::scene::CameraComponent* camera = state.scene->cameras.GetComponent(state.primary_camera_entity);
    if (camera == nullptr) return ELISA_RENDER_SCENE_BACKEND_FAILED;
    state.camera_entity = state.primary_camera_entity;
    state.camera = camera;
    state.path->camera = camera;
    sync_camera_settings(state, *camera);
    return resize_unlocked(state, state.width, state.height);
}

extern "C" int32_t elisa_render_scene_v1_destroy_camera(int64_t handle) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    size_t slot = MAX_RENDER_CAMERAS;
    if (!valid_camera_handle(state, handle, slot)) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    if (state.camera_entity == state.cameras[slot].entity) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    state.scene->Entity_Remove(state.cameras[slot].entity);
    state.cameras[slot].entity = wi::ecs::INVALID_ENTITY;
    state.cameras[slot].live = false;
    refresh_active_camera(state);
    return ELISA_RENDER_SCENE_OK;
}
