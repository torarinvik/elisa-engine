// Private implementation fragment: animated clones of a cooked source and the
// non-blocking pose consumption query used by per-frame pose scheduling.

// Builds another animated instance from the source's decoded cooked asset.
// The package is not read or decoded again; the clone gets its own Wicked
// mesh, armature, bones and clip state because Wicked deforms per mesh.
// Sources without animation data, clones of static meshes, and packages that
// carry cameras or lights (which a clone would duplicate) are rejected.
extern "C" int64_t elisa_render_scene_v1_create_animated_mesh_instance(
    int64_t source_handle,
    float px, float py, float pz,
    float qx, float qy, float qz, float qw,
    float sx, float sy, float sz,
    float red, float green, float blue, float alpha) {
    if (!valid_transform(px, py, pz, qx, qy, qz, qw, sx, sy, sz) ||
        !valid_color(red, green, blue, alpha)) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    size_t source_slot = MAX_INSTANCES;
    if (!valid_handle(state, source_handle, source_slot)) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    // Copy the pointer: installing may reuse slots but never the source's.
    const std::shared_ptr<const elisa::assets::CookedGeometry> asset =
        state.instances[source_slot].animated_asset;
    if (asset == nullptr || !asset->cameras.empty() || !asset->lights.empty()) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    return install_cooked_mesh_asset(state, asset, px, py, pz, qx, qy, qz, qw, sx, sy, sz,
        red, green, blue, alpha, nullptr);
}

// 0: no pose in flight; 1: the reading frame has not finished on the device;
// 2: consumed, so completing will not wait. Negative values are statuses.
constexpr int32_t ANIMATION_POSE_IDLE = 0;
constexpr int32_t ANIMATION_POSE_IN_FLIGHT = 1;
constexpr int32_t ANIMATION_POSE_CONSUMED = 2;
extern "C" int32_t elisa_render_scene_v1_animation_pose_state(int64_t handle) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    size_t slot = MAX_INSTANCES;
    if (!valid_handle(state, handle, slot)) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    const InstanceSlot& instance = state.instances[slot];
    if (state.animation_submission == nullptr || instance.animation_submission.owner == 0 ||
        !state.animation_submission->pending(instance.animation_submission)) return ANIMATION_POSE_IDLE;
    wi::graphics::GraphicsDevice* device = wi::graphics::GetDevice();
    if (device == nullptr) return ELISA_RENDER_SCENE_BACKEND_FAILED;
    if (device->GetFrameCount() < instance.animation_consume_frame) return ANIMATION_POSE_IN_FLIGHT;
    if (!device->SupportsFrameCompletionQuery()) {
        const uint64_t retired = instance.animation_consume_frame + device->GetBufferCount();
        return device->GetFrameCount() >= retired ? ANIMATION_POSE_CONSUMED : ANIMATION_POSE_IN_FLIGHT;
    }
    return device->IsFrameComplete(instance.animation_consume_frame)
        ? ANIMATION_POSE_CONSUMED : ANIMATION_POSE_IN_FLIGHT;
}

#if defined(ELISA_RENDER_SCENE_TEST_PROBE)
// 1 when both instances hold the same decoded asset with `expected_owners`
// references, yet own distinct mesh, armature and bone entities.
extern "C" int32_t elisa_render_scene_v1_test_animated_asset_shared(
    int64_t first_handle, int64_t second_handle, int32_t expected_owners) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state)) return 0;
    size_t first = MAX_INSTANCES;
    size_t second = MAX_INSTANCES;
    if (!valid_handle(state, first_handle, first) || !valid_handle(state, second_handle, second) ||
        first == second) return 0;
    const InstanceSlot& a = state.instances[first];
    const InstanceSlot& b = state.instances[second];
    if (a.animated_asset == nullptr || a.animated_asset != b.animated_asset ||
        a.animated_asset.use_count() != expected_owners || a.entity == b.entity) return 0;
    const auto* armature_a = state.scene->armatures.GetComponent(a.entity);
    const auto* armature_b = state.scene->armatures.GetComponent(b.entity);
    const auto* mesh_a = state.scene->meshes.GetComponent(a.entity);
    const auto* mesh_b = state.scene->meshes.GetComponent(b.entity);
    if (armature_a == nullptr || armature_b == nullptr || mesh_a == nullptr || mesh_b == nullptr ||
        mesh_a == mesh_b || mesh_a->armatureID != a.entity || mesh_b->armatureID != b.entity ||
        armature_a->boneCollection.size() != armature_b->boneCollection.size()) return 0;
    for (wi::ecs::Entity bone : armature_a->boneCollection)
        for (wi::ecs::Entity other : armature_b->boneCollection)
            if (bone == other) return 0;
    return a.animation_clips.size() == b.animation_clips.size() &&
        a.skin_joints.size() == b.skin_joints.size() ? 1 : 0;
}

// Owners of an instance's decoded animated asset, or -1 without one.
extern "C" int32_t elisa_render_scene_v1_test_animated_asset_owners(int64_t handle) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state)) return -1;
    size_t slot = MAX_INSTANCES;
    if (!valid_handle(state, handle, slot) || state.instances[slot].animated_asset == nullptr) return -1;
    return int32_t(state.instances[slot].animated_asset.use_count());
}

// 1 when both animated instances retain the same immutable ozz rig/clip library.
extern "C" int32_t elisa_render_scene_v1_test_ozz_library_shared(
    int64_t first_handle, int64_t second_handle) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state)) return 0;
    size_t first = MAX_INSTANCES;
    size_t second = MAX_INSTANCES;
    if (!valid_handle(state, first_handle, first) || !valid_handle(state, second_handle, second) ||
        first == second) return 0;
    const auto& first_library = state.instances[first].ozz_library;
    const auto& second_library = state.instances[second].ozz_library;
    return first_library != nullptr && first_library == second_library ? 1 : 0;
}
#endif
