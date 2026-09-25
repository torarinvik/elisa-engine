bool ensure_animation_submission(RenderSceneService& state, size_t slot) {
    if (state.animation_submission == nullptr) return false;
    InstanceSlot& instance = state.instances[slot];
    if (instance.animation_submission.owner != 0) return true;
    if (state.scene->armatures.GetComponent(instance.entity) == nullptr ||
        state.scene->meshes.GetComponent(instance.entity) == nullptr) return false;
    const size_t placement_count = instance.imported_mesh_entities.size() +
        instance.snapshot_placement_entities.size();
    if (placement_count + 1 > probe::AnimationSubmissionBridge::MAX_MESHES) return false;
    std::array<wi::ecs::Entity, probe::AnimationSubmissionBridge::MAX_MESHES> meshes{};
    meshes[0] = instance.entity;
    auto next_mesh = std::copy(instance.imported_mesh_entities.begin(),
        instance.imported_mesh_entities.end(), meshes.begin() + 1);
    std::copy(instance.snapshot_placement_entities.begin(),
        instance.snapshot_placement_entities.end(), next_mesh);
    const probe::NativeAnimationHandle handle = state.animation_submission->create(
        instance.entity, meshes.data(), uint32_t(placement_count + 1));
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
constexpr float SKIN_ANCESTOR_WICKED_Z = -0.25f;
enum AnimationStateProbeMatch : int32_t {
    ANIMATION_BONE_MATCH = 1 << 0,
    ANIMATION_MORPH_MATCH = 1 << 1,
    ANIMATION_PLACEMENT_MATCH = 1 << 2,
    ANIMATION_PENDING_MATCH = 1 << 3,
    ANIMATION_HIERARCHY_MATCH = 1 << 4,
};

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
    const auto& placements = instance.snapshot_placement_entities.empty()
        ? instance.imported_mesh_entities : instance.snapshot_placement_entities;
    if (armature == nullptr || armature->boneCollection.empty() || mesh == nullptr ||
        placements.size() != 1 || mesh->vertex_positions.size() != 12 ||
        mesh->indices.size() != 18 || mesh->subsets.size() != 3 || mesh->vertex_boneindices.size() != 12) return 0;
    const wi::scene::TransformComponent* bone = state.scene->transforms.GetComponent(armature->boneCollection[0]);
    if (bone == nullptr) return 0;
    const float expected_wicked_x = -expected_x;
    const bool bone_match = std::abs(bone->translation_local.x - expected_wicked_x) < 1.0e-5f;
    const bool hierarchy_match = std::abs(bone->world.m[3][2] - SKIN_ANCESTOR_WICKED_Z) < 1.0e-5f;
    const bool morph_match = mesh->morph_targets.empty()
        ? std::abs(expected_morph) < 1.0e-5f
        : std::abs(mesh->morph_targets[0].weight - expected_morph) < 1.0e-5f;
    const wi::scene::MeshComponent* placement =
        state.scene->meshes.GetComponent(placements[0]);
    const bool placement_match = placement != nullptr && placement->armatureID == instance.entity &&
        placement->vertex_positions.size() == 12 && placement->indices.size() == 18 &&
        placement->subsets.size() == 3 && placement->vertex_boneindices.size() == 12 &&
        placement->morph_targets.size() == mesh->morph_targets.size() &&
        (placement->morph_targets.empty() ? std::abs(expected_morph) < 1.0e-5f :
            std::abs(placement->morph_targets[0].weight - expected_morph) < 1.0e-5f);
    bool placement_vertices_match = placement != nullptr && placement->vertex_positions.size() ==
        mesh->vertex_positions.size();
    for (size_t vertex = 0; placement_vertices_match && vertex < mesh->vertex_positions.size(); ++vertex) {
        const XMFLOAT3& root_position = mesh->vertex_positions[vertex];
        const XMFLOAT3& child_position = placement->vertex_positions[vertex];
        placement_vertices_match = root_position.x == child_position.x && root_position.y == child_position.y &&
            root_position.z == child_position.z;
    }
    const bool pending_match = state.animation_submission->pending(instance.animation_submission) ==
        (expected_pending != 0);
    return (bone_match ? ANIMATION_BONE_MATCH : 0) | (morph_match ? ANIMATION_MORPH_MATCH : 0) |
        (placement_match && placement_vertices_match ? ANIMATION_PLACEMENT_MATCH : 0) |
        (pending_match ? ANIMATION_PENDING_MATCH : 0) |
        (hierarchy_match ? ANIMATION_HIERARCHY_MATCH : 0);
}

extern "C" int32_t elisa_render_scene_v1_test_animation_joint_y(
    int64_t handle, uint32_t joint_index, float expected_y) {
    if (!std::isfinite(expected_y)) return 0;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state)) return 0;
    size_t slot = MAX_INSTANCES;
    if (!valid_handle(state, handle, slot)) return 0;
    const InstanceSlot& instance = state.instances[slot];
    if (instance.animation_clips.size() != 2 || instance.joint_entities.size() != 4 ||
        joint_index >= instance.joint_entities.size() ||
        instance.animation_clips[0].name != "lift" || instance.animation_clips[1].name != "drop") return 0;
    const wi::scene::TransformComponent* joint =
        state.scene->transforms.GetComponent(instance.joint_entities[joint_index]);
    return joint != nullptr && std::abs(joint->translation_local.y - expected_y) < 2.0e-3f ? 1 : 0;
}

extern "C" int32_t elisa_render_scene_v1_test_animation_root_pose(
    int64_t handle, float expected_x, float expected_y, float expected_z) {
    if (!std::isfinite(expected_x) || !std::isfinite(expected_y) || !std::isfinite(expected_z)) return 0;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state)) return 0;
    size_t slot = MAX_INSTANCES;
    if (!valid_handle(state, handle, slot)) return 0;
    const InstanceSlot& instance = state.instances[slot];
    if (instance.displayed_joint_pose.size() != instance.joint_entities.size() * 10) return 0;
    int32_t root_joint = instance.root_motion_joint;
    if (root_joint < 0) {
        for (size_t index = 0; index < instance.skin_joints.size(); ++index) {
            if (instance.skin_joints[index].name == "root") {
                root_joint = int32_t(index);
                break;
            }
        }
    }
    if (root_joint < 0) return 0;
    const float* root = instance.displayed_joint_pose.data() + size_t(root_joint) * 10;
    return std::abs(root[0] - expected_x) < 2.0e-3f &&
        std::abs(root[1] - expected_y) < 2.0e-3f &&
        std::abs(root[2] - expected_z) < 2.0e-3f ? 1 : 0;
}

extern "C" int32_t elisa_render_scene_v1_test_skin_mesh_relative_root(
    int64_t handle, float expected_world_z) {
    if (!std::isfinite(expected_world_z)) return 0;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state)) return 0;
    size_t slot = MAX_INSTANCES;
    if (!valid_handle(state, handle, slot)) return 0;
    const wi::scene::ArmatureComponent* armature =
        state.scene->armatures.GetComponent(state.instances[slot].entity);
    if (armature == nullptr || armature->boneCollection.empty()) return 0;
    const wi::scene::TransformComponent* bone =
        state.scene->transforms.GetComponent(armature->boneCollection[0]);
    return bone != nullptr && std::abs(bone->world.m[3][2] - expected_world_z) < 1.0e-5f ? 1 : 0;
}

extern "C" int32_t elisa_render_scene_v1_test_multi_skin_layout(int64_t handle) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state)) return 0;
    size_t slot = MAX_INSTANCES;
    if (!valid_handle(state, handle, slot)) return 0;
    const InstanceSlot& instance = state.instances[slot];
    const wi::scene::ArmatureComponent* armature = state.scene->armatures.GetComponent(instance.entity);
    const wi::scene::MeshComponent* first = state.scene->meshes.GetComponent(instance.entity);
    if (armature == nullptr || first == nullptr || instance.skin_joints.size() != 6 ||
        armature->boneCollection.size() != 4 || instance.imported_mesh_entities.size() != 1 ||
        first->armatureID != instance.entity || first->vertex_boneindices.size() != 12 ||
        first->vertex_boneweights.size() != 12) return 0;
    const wi::scene::MeshComponent* second =
        state.scene->meshes.GetComponent(instance.imported_mesh_entities[0]);
    if (second == nullptr || second->armatureID != instance.entity ||
        second->vertex_boneindices.size() != 12 || second->vertex_boneweights.size() != 12) return 0;
    return first->vertex_boneindices[0].x == 0 && first->vertex_boneindices[6].x == 1 &&
        second->vertex_boneindices[0].x == 2 && second->vertex_boneindices[6].x == 3 &&
        first->vertex_boneweights[0].x > 0.99f && second->vertex_boneweights[0].x > 0.99f ? 1 : 0;
}

extern "C" int32_t elisa_render_scene_v1_test_mixed_skin_layout(int64_t handle) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state)) return 0;
    size_t slot = MAX_INSTANCES;
    if (!valid_handle(state, handle, slot)) return 0;
    const InstanceSlot& instance = state.instances[slot];
    const wi::scene::ArmatureComponent* armature = state.scene->armatures.GetComponent(instance.entity);
    const wi::scene::MeshComponent* skinned = state.scene->meshes.GetComponent(instance.entity);
    if (armature == nullptr || skinned == nullptr || instance.skin_joints.size() != 5 ||
        armature->boneCollection.size() != 3 || instance.imported_mesh_entities.size() != 1 ||
        skinned->armatureID != instance.entity || skinned->vertex_boneindices.size() != 12 ||
        skinned->vertex_boneweights.size() != 12) return 0;
    const wi::scene::MeshComponent* fixed =
        state.scene->meshes.GetComponent(instance.imported_mesh_entities[0]);
    if (fixed == nullptr || fixed->armatureID != instance.entity ||
        fixed->vertex_boneindices.size() != 12 || fixed->vertex_boneweights.size() != 12) return 0;
    return skinned->vertex_boneindices[0].x == 0 && skinned->vertex_boneindices[6].x == 1 &&
        fixed->vertex_boneindices[0].x == 2 && fixed->vertex_boneindices[6].x == 2 &&
        skinned->vertex_boneweights[0].x > 0.99f && fixed->vertex_boneweights[0].x > 0.99f ? 1 : 0;
}

extern "C" int32_t elisa_render_scene_v1_test_morph_animation_weights(
    int64_t handle, float first_weight, float second_weight) {
    if (!std::isfinite(first_weight) || !std::isfinite(second_weight)) return 0;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state)) return 0;
    size_t slot = MAX_INSTANCES;
    if (!valid_handle(state, handle, slot)) return 0;
    const InstanceSlot& instance = state.instances[slot];
    const wi::scene::MeshComponent* first = state.scene->meshes.GetComponent(instance.entity);
    if (!instance.skin_joints.empty() || instance.joint_entities.size() != 0 ||
        instance.animation_clips.size() != 1 || instance.animation_clip < -1 ||
        instance.animation_clip >= int32_t(instance.animation_clips.size()) ||
        first == nullptr || first->morph_targets.size() != 1 ||
        instance.imported_mesh_entities.size() != 1) return 0;
    const wi::scene::MeshComponent* second =
        state.scene->meshes.GetComponent(instance.imported_mesh_entities[0]);
    return second != nullptr && second->morph_targets.size() == 1 &&
        std::abs(first->morph_targets[0].weight - first_weight) < 1.0e-5f &&
        std::abs(second->morph_targets[0].weight - second_weight) < 1.0e-5f ? 1 : 0;
}

extern "C" int32_t elisa_render_scene_v1_test_morph_transition_weights(
    int64_t handle, float first_weight, float second_weight) {
    if (!std::isfinite(first_weight) || !std::isfinite(second_weight)) return 0;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state)) return 0;
    size_t slot = MAX_INSTANCES;
    if (!valid_handle(state, handle, slot)) return 0;
    const InstanceSlot& instance = state.instances[slot];
    const wi::scene::MeshComponent* first = state.scene->meshes.GetComponent(instance.entity);
    if (!instance.skin_joints.empty() || instance.joint_entities.size() != 0 ||
        instance.animation_clips.size() != 2 ||
        instance.animation_clips[0].name != "weights" ||
        instance.animation_clips[1].name != "weights-return" ||
        first == nullptr || first->morph_targets.size() != 1 ||
        instance.imported_mesh_entities.size() != 1) return 0;
    const wi::scene::MeshComponent* second =
        state.scene->meshes.GetComponent(instance.imported_mesh_entities[0]);
    return second != nullptr && second->morph_targets.size() == 1 &&
        std::abs(first->morph_targets[0].weight - first_weight) < 1.0e-5f &&
        std::abs(second->morph_targets[0].weight - second_weight) < 1.0e-5f ? 1 : 0;
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
        const float expected_wicked_x = -expected_x;
        const bool bone_match = std::abs(bone->translation_local.x - expected_wicked_x) < 1.0e-5f;
        const bool bounds_match = std::abs(armature->aabb._min.x -
                (expected_wicked_x - ANIMATION_BONE_BOUND_RADIUS)) < 1.0e-5f &&
            std::abs(armature->aabb._max.x -
                (expected_wicked_x + ANIMATION_BONE_BOUND_RADIUS)) < 1.0e-5f;
        const bool pending_match = state.animation_submission->pending(instance.animation_submission) ==
            (expected_pending != 0);
        return bone_match && bounds_match && pending_match;
    };
    return matches(first_slot, first_x) && matches(second_slot, second_x) ? 1 : 0;
}

extern "C" int32_t elisa_render_scene_v1_test_inverse_bind(
    int64_t handle, uint32_t cluster, uint32_t row, uint32_t column, float expected) {
    if (row >= 4 || column >= 4 || !std::isfinite(expected)) return 0;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state)) return 0;
    size_t slot = MAX_INSTANCES;
    if (!valid_handle(state, handle, slot)) return 0;
    const wi::scene::ArmatureComponent* armature = state.scene->armatures.GetComponent(state.instances[slot].entity);
    if (armature == nullptr || cluster >= armature->inverseBindMatrices.size()) return 0;
    const XMFLOAT4X4& matrix = armature->inverseBindMatrices[cluster];
    return std::abs(matrix.m[row][column] - expected) < 1.0e-5f ? 1 : 0;
}
#endif
