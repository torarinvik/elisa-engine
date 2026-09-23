// Private implementation fragment: per-instance animation control ABI.
// Transition flags shared by the plain and blended play entry points.
constexpr int32_t ANIMATION_BLEND_EASED = 1;
constexpr int32_t ANIMATION_BLEND_PHASE_MATCHED = 2;
constexpr int32_t ANIMATION_BLEND_CONTINUE_SAME_CLIP = 4;
constexpr int32_t ANIMATION_BLEND_FLAG_MASK = 7;

float clip_phase(const InstanceSlot& instance, int32_t clip_index, float time) {
    if (clip_index < 0 || clip_index >= int32_t(instance.animation_clips.size())) return 0.0f;
    const auto& clip = instance.animation_clips[size_t(clip_index)];
    if (!finite(clip.duration_seconds) || clip.duration_seconds <= 0.0f) return 0.0f;
    return std::clamp(time / clip.duration_seconds, 0.0f, 1.0f);
}

int32_t start_animation(int64_t handle, const char* clip_name, int32_t loop, float speed,
    float blend_seconds, int32_t flags) {
    if ((loop != 0 && loop != 1) || !finite(speed) || speed < 0.0f || speed > 16.0f ||
        !finite(blend_seconds) || blend_seconds < 0.0f || blend_seconds > 10.0f ||
        (flags & ~ANIMATION_BLEND_FLAG_MASK) != 0) {
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
    const bool has_current = instance.animation_clip >= 0 &&
        instance.animation_clip < int32_t(instance.animation_clips.size());
    if (has_current && next_clip == instance.animation_clip &&
        (flags & ANIMATION_BLEND_CONTINUE_SAME_CLIP) != 0) {
        // The cycle keeps its phase; only its cadence and looping change.
        instance.animation_speed = speed;
        instance.animation_loop = loop != 0;
        return apply_animation_pose(state, slot) ? ELISA_RENDER_SCENE_OK : ELISA_RENDER_SCENE_BACKEND_FAILED;
    }
    const float phase = has_current ? clip_phase(instance, instance.animation_clip, instance.animation_time) : 0.0f;
    if (has_current) {
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
    if ((flags & ANIMATION_BLEND_PHASE_MATCHED) != 0 && loop != 0) {
        // Locomotion cycles stay in step: the new cycle starts where the old
        // one was, so a walk-to-run switch does not restart the stride.
        instance.animation_time = phase * instance.animation_clips[size_t(next_clip)].duration_seconds;
    }
    instance.animation_speed = speed;
    instance.animation_loop = loop != 0;
    instance.blend_elapsed = 0.0f;
    instance.blend_duration = blend_seconds;
    instance.blend_eased = (flags & ANIMATION_BLEND_EASED) != 0;
    if (blend_seconds == 0.0f) instance.previous_animation_clip = -1;
    return apply_animation_pose(state, slot) ? ELISA_RENDER_SCENE_OK : ELISA_RENDER_SCENE_BACKEND_FAILED;
}

extern "C" int32_t elisa_render_scene_v1_play_animation(
    int64_t handle, const char* clip_name, int32_t loop, float speed, float blend_seconds) {
    return start_animation(handle, clip_name, loop, speed, blend_seconds, 0);
}

extern "C" int32_t elisa_render_scene_v1_play_animation_blended(
    int64_t handle, const char* clip_name, int32_t loop, float speed, float blend_seconds, int32_t flags) {
    return start_animation(handle, clip_name, loop, speed, blend_seconds, flags);
}

// Retimes the active clip in place. Speed-only changes must not restart the
// cycle or start a crossfade, otherwise an accelerating character freezes on
// the clip's first frame while its cadence keeps being re-scaled.
extern "C" int32_t elisa_render_scene_v1_set_animation_speed(int64_t handle, float speed) {
    if (!finite(speed) || speed < 0.0f || speed > 16.0f) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    size_t slot = MAX_INSTANCES;
    if (!valid_handle(state, handle, slot)) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    InstanceSlot& instance = state.instances[slot];
    if (instance.animation_clip < 0 || instance.animation_clip >= int32_t(instance.animation_clips.size())) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    instance.animation_speed = speed;
    return ELISA_RENDER_SCENE_OK;
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
    const bool has_morph_animation = std::any_of(instance.animation_clips.begin(),
        instance.animation_clips.end(), [](const auto& clip) { return !clip.morph_weights.empty(); });
    if (instance.joint_entities.size() != instance.skin_joints.size() ||
        (instance.skin_joints.empty() && !has_morph_animation)) {
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
    instance.blend_eased = false;
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

// Returns the active clip's normalized time so game code can synchronize
// authored cues (for example footfalls) without duplicating clip durations or
// adding a native game-specific shim. A handle with no active clip is idle.
extern "C" float elisa_render_scene_v1_animation_progress(int64_t handle) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state)) return -1.0f;
    size_t slot = MAX_INSTANCES;
    if (!valid_handle(state, handle, slot)) return -1.0f;
    const InstanceSlot& instance = state.instances[slot];
    if (instance.animation_clip < 0 ||
        instance.animation_clip >= int32_t(instance.animation_clips.size())) {
        return 0.0f;
    }
    const auto& clip = instance.animation_clips[size_t(instance.animation_clip)];
    if (!finite(clip.duration_seconds) || clip.duration_seconds <= 0.0f) return 0.0f;
    return std::clamp(instance.animation_time / clip.duration_seconds, 0.0f, 1.0f);
}
