constexpr size_t MAX_SNAPSHOT_MESH_ASSETS = 32;
constexpr size_t MAX_SNAPSHOT_MATERIAL_ASSETS = 64;
constexpr size_t MAX_SNAPSHOT_SHARED_MESHES = MAX_INSTANCES * 2;
constexpr size_t MAX_SNAPSHOT_GEOMETRY_BYTES = 256u * 1024u * 1024u;
constexpr float DEFAULT_SNAPSHOT_ALPHA_CUTOFF = 0.5f;
constexpr float OPAQUE_SNAPSHOT_ALPHA_REF = 1.0f;
constexpr float DEFAULT_SNAPSHOT_EMISSIVE_STRENGTH = 1.0f;

struct SnapshotStageRow {
    int64_t existing_handle = 0;
    int64_t gameplay_epoch = 0;
    int64_t gameplay_id = 0;
    int64_t render_id = 0;
    uint64_t mesh_high = 0;
    uint64_t mesh_low = 0;
    uint64_t material_high = 0;
    uint64_t material_low = 0;
    float position[3] = {};
    float rotation[4] = {};
    float scale[3] = {};
    size_t reserved_slot = MAX_INSTANCES;
    uint64_t reserved_generation = 0;
    size_t shared_mesh_slot = MAX_INSTANCES;
    wi::ecs::Entity created_entity = wi::ecs::INVALID_ENTITY;
    std::vector<wi::ecs::Entity> created_joint_entities;
    std::vector<elisa::assets::CookedGeometry::SkinJoint> created_skin_joints;
    std::vector<elisa::assets::CookedGeometry::AnimationClip> created_animation_clips;
};

struct SnapshotMeshAssetSlot {
    uint64_t high = 0;
    uint64_t low = 0;
    std::filesystem::path canonical_path;
    elisa::assets::CookedGeometry geometry;
    size_t bytes = 0;
    bool live = false;
};

struct SnapshotMaterialAssetSlot {
    uint64_t high = 0;
    uint64_t low = 0;
    float base_color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float emissive[3] = {};
    float metallic = 0.0f;
    float roughness = 1.0f;
    float alpha_cutoff = DEFAULT_SNAPSHOT_ALPHA_CUTOFF;
    int32_t alpha_mode = ELISA_RENDER_SCENE_ALPHA_OPAQUE;
    wi::ecs::Entity material_entity = wi::ecs::INVALID_ENTITY;
    bool double_sided = false;
    bool live = false;
};

struct SnapshotSharedMesh {
    uint64_t mesh_high = 0;
    uint64_t mesh_low = 0;
    uint64_t material_high = 0;
    uint64_t material_low = 0;
    wi::ecs::Entity entity = wi::ecs::INVALID_ENTITY;
    size_t references = 0;
    size_t bytes = 0;
    bool live = false;
};
