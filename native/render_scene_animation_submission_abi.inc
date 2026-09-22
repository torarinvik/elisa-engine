bool ensure_animation_submission(RenderSceneService& state, size_t slot) {
    if (state.animation_submission == nullptr) return false;
    InstanceSlot& instance = state.instances[slot];
    if (instance.animation_submission.owner != 0) return true;
    if (state.scene->armatures.GetComponent(instance.entity) == nullptr ||
        state.scene->meshes.GetComponent(instance.entity) == nullptr) return false;
    const probe::NativeAnimationHandle handle = state.animation_submission->create(
        instance.entity, instance.entity);
    if (handle.owner == 0) return false;
    instance.animation_submission = handle;
    return true;
}

extern "C" int32_t elisa_render_scene_v1_submit_animation_pose(
    int64_t handle, const ElisaRenderSceneAnimationSubmission* submission) {
    if (submission == nullptr || submission->bone_count == 0 ||
        submission->bone_count > ELISA_RENDER_SCENE_MAX_ANIMATION_BONES ||
        submission->morph_count > ELISA_RENDER_SCENE_MAX_ANIMATION_MORPHS) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    size_t slot = MAX_INSTANCES;
    if (!valid_handle(state, handle, slot)) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    if (!ensure_animation_submission(state, slot)) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    const bool submitted = state.animation_submission->submit(
        state.instances[slot].animation_submission,
        reinterpret_cast<const XMFLOAT4X4*>(submission->bones), submission->bone_count,
        submission->morphs, submission->morph_count);
    return submitted ? ELISA_RENDER_SCENE_OK : ELISA_RENDER_SCENE_INVALID_ARGUMENT;
}

extern "C" int32_t elisa_render_scene_v1_complete_animation_pose(int64_t handle) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    size_t slot = MAX_INSTANCES;
    if (!valid_handle(state, handle, slot)) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    if (state.animation_submission == nullptr || state.instances[slot].animation_submission.owner == 0 ||
        !state.animation_submission->complete(state.instances[slot].animation_submission)) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    return ELISA_RENDER_SCENE_OK;
}

#if defined(ELISA_RENDER_SCENE_TEST_PROBE)
constexpr float ANIMATION_BONE_BOUND_RADIUS = 1.0f;

extern "C" int32_t elisa_render_scene_v1_test_animation_submission(
    int64_t handle, float expected_x, float expected_morph, int32_t expected_pending) {
    if (!std::isfinite(expected_x) || !std::isfinite(expected_morph) ||
        (expected_pending != 0 && expected_pending != 1)) return 0;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state) || state.animation_submission == nullptr) return 0;
    size_t slot = MAX_INSTANCES;
    if (!valid_handle(state, handle, slot)) return 0;
    const InstanceSlot& instance = state.instances[slot];
    const wi::scene::ArmatureComponent* armature = state.scene->armatures.GetComponent(instance.entity);
    const wi::scene::MeshComponent* mesh = state.scene->meshes.GetComponent(instance.entity);
    if (armature == nullptr || armature->boneCollection.empty() || mesh == nullptr) return 0;
    const wi::scene::TransformComponent* bone = state.scene->transforms.GetComponent(armature->boneCollection[0]);
    if (bone == nullptr) return 0;
    const bool bone_match = std::abs(bone->translation_local.x - expected_x) < 1.0e-5f;
    const bool morph_match = mesh->morph_targets.empty()
        ? std::abs(expected_morph) < 1.0e-5f
        : std::abs(mesh->morph_targets[0].weight - expected_morph) < 1.0e-5f;
    const bool pending_match = state.animation_submission->pending(instance.animation_submission) ==
        (expected_pending != 0);
    return bone_match && morph_match && pending_match ? 1 : 0;
}

extern "C" int32_t elisa_render_scene_v1_test_animation_instances(
    int64_t first_handle, float first_x, int64_t second_handle, float second_x,
    int32_t expected_pending) {
    if (!std::isfinite(first_x) || !std::isfinite(second_x) ||
        (expected_pending != 0 && expected_pending != 1)) return 0;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state) || state.animation_submission == nullptr) return 0;
    size_t first_slot = MAX_INSTANCES;
    size_t second_slot = MAX_INSTANCES;
    if (!valid_handle(state, first_handle, first_slot) ||
        !valid_handle(state, second_handle, second_slot) || first_slot == second_slot) return 0;

    const auto matches = [&](size_t slot, float expected_x) {
        const InstanceSlot& instance = state.instances[slot];
        const wi::scene::ArmatureComponent* armature = state.scene->armatures.GetComponent(instance.entity);
        if (armature == nullptr || armature->boneCollection.empty()) return false;
        const wi::scene::TransformComponent* bone =
            state.scene->transforms.GetComponent(armature->boneCollection[0]);
        if (bone == nullptr) return false;
        const bool bone_match = std::abs(bone->translation_local.x - expected_x) < 1.0e-5f;
        const bool bounds_match = std::abs(armature->aabb._min.x -
                (expected_x - ANIMATION_BONE_BOUND_RADIUS)) < 1.0e-5f &&
            std::abs(armature->aabb._max.x -
                (expected_x + ANIMATION_BONE_BOUND_RADIUS)) < 1.0e-5f;
        const bool pending_match = state.animation_submission->pending(instance.animation_submission) ==
            (expected_pending != 0);
        return bone_match && bounds_match && pending_match;
    };
    return matches(first_slot, first_x) && matches(second_slot, second_x) ? 1 : 0;
}
#endif
