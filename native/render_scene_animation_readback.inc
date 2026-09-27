// Read a coherent visible pose under the scene owner-thread lock.
extern "C" int32_t elisa_render_scene_v1_read_animation_pose(
        int64_t handle, ElisaRenderSceneAnimationReadback* output) {
    if (output == nullptr) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    size_t slot = MAX_INSTANCES;
    if (!valid_handle(state, handle, slot)) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    const InstanceSlot& instance = state.instances[slot];
    const size_t count = instance.skin_joints.size();
    if (count > ELISA_RENDER_SCENE_MAX_ANIMATION_BONES) return ELISA_RENDER_SCENE_CAPACITY;
    if (count == 0 || instance.displayed_joint_pose.size() != count * 10)
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    ElisaRenderSceneAnimationReadback snapshot{};
    for (size_t index = 0; index < count; ++index) {
        const int64_t parent = instance.skin_joints[index].parent_index;
        if (parent < -1 || parent >= int64_t(index)) return ELISA_RENDER_SCENE_BACKEND_FAILED;
        snapshot.parents[index] = parent;
        for (size_t component = 0; component < 10; ++component) {
            const float value = instance.displayed_joint_pose[index * 10 + component];
            if (!std::isfinite(value)) return ELISA_RENDER_SCENE_BACKEND_FAILED;
            snapshot.locals[index * 10 + component] = value;
        }
    }
    snapshot.count = uint32_t(count);
    *output = snapshot; // Failed reads leave caller state unchanged.
    return ELISA_RENDER_SCENE_OK;
}

// One-frame post-sample override. The next advance resamples the clips; an
// interruption before that advance captures these corrected displayed locals.
extern "C" int32_t elisa_render_scene_v1_override_animation_pose(
        int64_t handle, const ElisaRenderSceneAnimationReadback* input) {
    if (input == nullptr || input->count == 0 || input->count > ELISA_RENDER_SCENE_MAX_ANIMATION_BONES)
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    size_t slot = MAX_INSTANCES;
    if (!valid_handle(state, handle, slot)) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    InstanceSlot& instance = state.instances[slot];
    if (input->count != instance.skin_joints.size() || input->count != instance.joint_entities.size())
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    // Validate every joint before mutating the displayed pose or scene.
    for (size_t index = 0; index < input->count; ++index) {
        if (input->parents[index] != instance.skin_joints[index].parent_index ||
            state.scene->transforms.GetComponent(instance.joint_entities[index]) == nullptr)
            return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
        const float* local = input->locals + index * 10;
        for (size_t component = 0; component < 10; ++component)
            if (!std::isfinite(local[component])) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
        float norm = 0;
        for (size_t component = 3; component < 7; ++component) norm += local[component]*local[component];
        if (std::abs(norm - 1.f) > .002f || local[7] == 0 || local[8] == 0 || local[9] == 0)
            return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    if (state.scene->transforms.GetComponent(instance.entity) == nullptr)
        return ELISA_RENDER_SCENE_BACKEND_FAILED;
    instance.displayed_joint_pose.assign(input->locals, input->locals + input->count * 10);
    instance.animation_pose_overridden = true;
    return apply_displayed_joint_transforms(state, slot) ? ELISA_RENDER_SCENE_OK : ELISA_RENDER_SCENE_BACKEND_FAILED;
}

extern "C" int32_t elisa_render_scene_v1_animation_joint_index(
        int64_t handle, const char* joint_name, uint32_t* output) {
    if (joint_name == nullptr || output == nullptr || joint_name[0] == '\0')
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    size_t length = 0;
    while (length <= 256 && joint_name[length] != '\0') ++length;
    if (length > 256) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    size_t slot = MAX_INSTANCES;
    if (!valid_handle(state, handle, slot)) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    const auto& joints = state.instances[slot].skin_joints;
    for (size_t index = 0; index < joints.size(); ++index) {
        if (joints[index].name == joint_name) {
            if (index >= ELISA_RENDER_SCENE_MAX_ANIMATION_BONES) return ELISA_RENDER_SCENE_CAPACITY;
            *output = uint32_t(index);
            return ELISA_RENDER_SCENE_OK;
        }
    }
    return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
}

// Native integration probe checks scene transforms, not the readback buffer.
extern "C" int32_t elisa_render_scene_v1_test_animation_joint_position(
        int64_t handle, uint32_t joint_index, float x, float y, float z) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return 0;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state)) return 0;
    size_t slot = MAX_INSTANCES;
    if (!valid_handle(state, handle, slot)) return 0;
    const auto& entities = state.instances[slot].joint_entities;
    if (joint_index >= entities.size()) return 0;
    const auto* transform = state.scene->transforms.GetComponent(entities[joint_index]);
    if (transform == nullptr) return 0;
    const XMFLOAT3 position = probe::coordinates::from_wicked(transform->GetPosition());
    return std::abs(position.x-x) < .0002f && std::abs(position.y-y) < .0002f &&
        std::abs(position.z-z) < .0002f ? 1 : 0;
}
