// Private implementation fragment: per-instance animation control ABI.
extern "C" int32_t elisa_render_scene_v1_play_animation(
    int64_t handle, const char* clip_name, int32_t loop, float speed, float blend_seconds) {
    if ((loop != 0 && loop != 1) || !finite(speed) || speed < 0.0f || speed > 16.0f ||
        !finite(blend_seconds) || blend_seconds < 0.0f || blend_seconds > 10.0f) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    size_t slot = MAX_INSTANCES;
    if (!valid_handle(state, handle, slot)) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    InstanceSlot& instance = state.instances[slot];
    const int32_t next_clip = find_animation_clip(instance, clip_name);
    if (next_clip < 0 || instance.joint_entities.size() != instance.skin_joints.size()) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    if (instance.animation_clip >= 0 && instance.animation_clip < int32_t(instance.animation_clips.size())) {
        instance.previous_animation_clip = instance.animation_clip;
        instance.previous_animation_time = instance.animation_time;
        instance.previous_animation_speed = instance.animation_speed;
        instance.previous_animation_loop = instance.animation_loop;
    } else {
        instance.previous_animation_clip = -1;
        instance.previous_animation_time = 0.0f;
        instance.previous_animation_speed = 1.0f;
        instance.previous_animation_loop = true;
    }
    instance.animation_clip = next_clip;
    instance.animation_time = 0.0f;
    instance.animation_speed = speed;
    instance.animation_loop = loop != 0;
    instance.blend_elapsed = 0.0f;
    instance.blend_duration = blend_seconds;
    if (blend_seconds == 0.0f) instance.previous_animation_clip = -1;
    return apply_animation_pose(state, slot) ? ELISA_RENDER_SCENE_OK : ELISA_RENDER_SCENE_BACKEND_FAILED;
}

extern "C" int32_t elisa_render_scene_v1_stop_animation(int64_t handle, float blend_seconds) {
    if (!finite(blend_seconds) || blend_seconds < 0.0f || blend_seconds > 10.0f) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    size_t slot = MAX_INSTANCES;
    if (!valid_handle(state, handle, slot)) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    InstanceSlot& instance = state.instances[slot];
    if (instance.skin_joints.empty() || instance.joint_entities.size() != instance.skin_joints.size()) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    if (instance.animation_clip >= 0 && instance.animation_clip < int32_t(instance.animation_clips.size())) {
        instance.previous_animation_clip = instance.animation_clip;
        instance.previous_animation_time = instance.animation_time;
        instance.previous_animation_speed = instance.animation_speed;
        instance.previous_animation_loop = instance.animation_loop;
    } else if (instance.previous_animation_clip < 0) {
        instance.previous_animation_clip = -1;
        instance.previous_animation_time = 0.0f;
        instance.previous_animation_speed = 1.0f;
        instance.previous_animation_loop = true;
    }
    instance.animation_clip = -1;
    instance.animation_time = 0.0f;
    instance.blend_elapsed = 0.0f;
    instance.blend_duration = blend_seconds;
    if (blend_seconds == 0.0f) instance.previous_animation_clip = -1;
    return apply_animation_pose(state, slot) ? ELISA_RENDER_SCENE_OK : ELISA_RENDER_SCENE_BACKEND_FAILED;
}

extern "C" int32_t elisa_render_scene_v1_advance_animation(int64_t handle, float delta_seconds) {
    if (!finite(delta_seconds) || delta_seconds < 0.0f || delta_seconds > 1.0f) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    size_t slot = MAX_INSTANCES;
    if (!valid_handle(state, handle, slot)) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    InstanceSlot& instance = state.instances[slot];
    if ((instance.animation_clip >= int32_t(instance.animation_clips.size())) ||
        (instance.animation_clip < 0 && instance.previous_animation_clip < 0)) {
        return instance.animation_clip < 0 ? ELISA_RENDER_SCENE_OK : ELISA_RENDER_SCENE_BACKEND_FAILED;
    }
    if (instance.animation_clip >= 0) {
        instance.animation_time += delta_seconds * instance.animation_speed;
        if (instance.animation_loop) {
            const auto& clip = instance.animation_clips[size_t(instance.animation_clip)];
            instance.animation_time = animation_time(clip, instance.animation_time, true);
        } else {
            const auto& clip = instance.animation_clips[size_t(instance.animation_clip)];
            instance.animation_time = std::min(instance.animation_time, clip.duration_seconds);
        }
    }
    if (instance.previous_animation_clip >= 0) {
        if (instance.previous_animation_clip >= int32_t(instance.animation_clips.size())) {
            return ELISA_RENDER_SCENE_BACKEND_FAILED;
        }
        instance.previous_animation_time += delta_seconds * instance.previous_animation_speed;
        const auto& previous = instance.animation_clips[size_t(instance.previous_animation_clip)];
        instance.previous_animation_time = animation_time(previous, instance.previous_animation_time,
            instance.previous_animation_loop);
    }
    if (instance.blend_duration > 0.0f) {
        instance.blend_elapsed = std::min(instance.blend_duration, instance.blend_elapsed + delta_seconds);
        if (instance.blend_elapsed >= instance.blend_duration) instance.previous_animation_clip = -1;
    }
    return apply_animation_pose(state, slot) ? ELISA_RENDER_SCENE_OK : ELISA_RENDER_SCENE_BACKEND_FAILED;
}
