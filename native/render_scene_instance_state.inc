struct InstanceSlot {
    wi::ecs::Entity entity = wi::ecs::INVALID_ENTITY;
    uint64_t generation = 0;
    int64_t gameplay_epoch = 0;
    int64_t gameplay_id = 0;
    int64_t render_id = 0;
    uint64_t mesh_high = 0;
    uint64_t mesh_low = 0;
    uint64_t material_high = 0;
    uint64_t material_low = 0;
    size_t shared_mesh_slot = NO_SHARED_MESH;
    size_t snapshot_lod_level = 0;
    std::vector<size_t> snapshot_additional_shared_mesh_slots;
    // Set on a clone: the instance whose mesh and material it draws.
    wi::ecs::Entity shared_source = wi::ecs::INVALID_ENTITY;
    std::vector<wi::ecs::Entity> joint_entities;
    // Additional mesh entities for a cooked scene with multiple placements;
    // `entity` remains the root placement and owns the gameplay transform.
    std::vector<wi::ecs::Entity> imported_mesh_entities;
    std::vector<wi::ecs::Entity> snapshot_placement_entities;
    std::vector<wi::ecs::Entity> imported_camera_entities;
    std::vector<probe::NativeLightHandle> imported_light_handles;
    std::vector<probe::PickBinding> pick_bindings;
    std::vector<elisa::assets::CookedGeometry::SkinJoint> skin_joints;
    std::vector<elisa::assets::CookedGeometry::AnimationClip> animation_clips;
    std::vector<float> morph_default_weights;
    probe::NativeAnimationHandle animation_submission{};
    int32_t animation_clip = -1;
    int32_t previous_animation_clip = -1;
    float animation_time = 0.0f;
    float previous_animation_time = 0.0f;
    float animation_speed = 1.0f;
    float previous_animation_speed = 1.0f;
    // Visible pose retained when a new request interrupts an unfinished fade.
    std::vector<float> displayed_joint_pose;
    std::vector<float> interrupted_joint_pose;
    std::vector<float> displayed_morph_pose;
    std::vector<float> interrupted_morph_pose;
    float blend_elapsed = 0.0f;
    float blend_duration = 0.0f;
    bool blend_eased = false;
    bool animation_loop = true;
    bool previous_animation_loop = true;
    bool live = false;
};
