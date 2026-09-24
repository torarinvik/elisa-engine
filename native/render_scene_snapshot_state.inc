constexpr size_t MAX_SNAPSHOT_MESH_ASSETS = 32;
constexpr size_t MAX_SNAPSHOT_MATERIAL_ASSETS = 64;
constexpr size_t MAX_SNAPSHOT_TEXTURE_ASSETS = 128;
constexpr size_t MAX_SNAPSHOT_MATERIAL_SETS = 32;
constexpr size_t MAX_SNAPSHOT_MATERIAL_SET_SLOTS = elisa::assets::MAX_GEOMETRY_MATERIAL_SLOTS;
constexpr size_t MAX_SNAPSHOT_TEXTURE_SOURCE_BYTES = 256u * 1024u * 1024u;
constexpr size_t SNAPSHOT_MATERIAL_TEXTURE_SLOTS = elisa::assets::SLOT_MATERIAL_TEXTURES;
constexpr size_t MAX_SNAPSHOT_SHARED_MESHES = MAX_INSTANCES * 2;
constexpr size_t MAX_SNAPSHOT_GEOMETRY_BYTES = 256u * 1024u * 1024u;
constexpr size_t MAX_SNAPSHOT_ASSET_REQUESTS = elisa::assets::SerialJobWorker<int>::MAX_JOBS;
constexpr uint32_t MAX_SNAPSHOT_ASSET_ADOPTIONS = MAX_SNAPSHOT_ASSET_REQUESTS;
constexpr size_t MAX_SNAPSHOT_DECODED_TEXTURE_BYTES = elisa::rendering::textures::MAX_DECODED_IMAGE_BYTES;
constexpr float DEFAULT_SNAPSHOT_ALPHA_CUTOFF = 0.5f;
constexpr float OPAQUE_SNAPSHOT_ALPHA_REF = 1.0f;
constexpr float DEFAULT_SNAPSHOT_EMISSIVE_STRENGTH = 1.0f;
constexpr float MAX_SNAPSHOT_TINT_COLOR_SCALE = 4.0f;

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
    // Per-instance ObjectComponent multipliers; neutral unless stage_tint runs.
    float tint_color[3] = {1.0f, 1.0f, 1.0f};
    float tint_emissive[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    bool tinted = false;
    size_t reserved_slot = MAX_INSTANCES;
    uint64_t reserved_generation = 0;
    size_t shared_mesh_slot = NO_SHARED_MESH;
    std::vector<size_t> created_additional_shared_mesh_slots;
    wi::ecs::Entity created_entity = wi::ecs::INVALID_ENTITY;
    std::vector<wi::ecs::Entity> created_placement_entities;
    std::vector<wi::ecs::Entity> created_joint_entities;
    std::vector<elisa::assets::CookedGeometry::SkinJoint> created_skin_joints;
    std::vector<elisa::assets::CookedGeometry::AnimationClip> created_animation_clips;
    std::vector<float> created_morph_default_weights;
};

struct SnapshotMeshAssetSlot {
    uint64_t high = 0;
    uint64_t low = 0;
    // The path as registered, so a later request compares without file IO.
    std::string requested_path;
    std::filesystem::path canonical_path;
    elisa::assets::CookedGeometry geometry;
    std::array<elisa::assets::CookedGeometry, elisa::assets::MAX_LOD_LEVELS - 1> additional_lod_geometry{};
    elisa::assets::LodManifest lod_manifest;
    size_t lod_level_count = 1;
    size_t bytes = 0;
    uint64_t last_used = 0;
    bool live = false;
};

const elisa::assets::CookedGeometry& snapshot_mesh_geometry(
        const SnapshotMeshAssetSlot& asset, size_t level) {
    return level == 0 ? asset.geometry : asset.additional_lod_geometry[level - 1];
}

struct SnapshotMaterialAssetSlot {
    uint64_t high = 0;
    uint64_t low = 0;
    float base_color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float emissive[3] = {};
    float metallic = 0.0f;
    float roughness = 1.0f;
    float alpha_cutoff = DEFAULT_SNAPSHOT_ALPHA_CUTOFF;
    float normal_scale = 1.0f;
    float occlusion_strength = 1.0f;
    float clearcoat_factor = 0.0f;
    float clearcoat_roughness = 0.0f;
    float clearcoat_normal_scale = 1.0f;
    int32_t alpha_mode = ELISA_RENDER_SCENE_ALPHA_OPAQUE;
    std::array<uint64_t, SNAPSHOT_MATERIAL_TEXTURE_SLOTS> texture_high{};
    std::array<uint64_t, SNAPSHOT_MATERIAL_TEXTURE_SLOTS> texture_low{};
    wi::ecs::Entity material_entity = wi::ecs::INVALID_ENTITY;
    bool double_sided = false;
    // Occlusion samples either the packed surface texture or the dedicated
    // occlusion texture slot.
    bool occlusion = false;
    bool live = false;
};

struct SnapshotTextureAssetSlot {
    uint64_t high = 0;
    uint64_t low = 0;
    std::string requested_path;
    std::filesystem::path canonical_path;
    // PNG/JPEG sections decode to bounded CPU pixels; KTX2 stays encoded until
    // material-role upload. Owner-thread import creates role-specific resources.
    std::string section;
    std::string resource_stem;
    elisa::rendering::textures::DecodedImage decoded;
    std::vector<uint8_t> encoded_ktx2;
    std::array<wi::Resource, SNAPSHOT_MATERIAL_TEXTURE_SLOTS> resources{};
    size_t source_bytes = 0;
    elisa::assets::EncodedImageFormat format = elisa::assets::EncodedImageFormat::Png;
    const char* extension = nullptr;
    uint64_t last_used = 0;
    bool decoded_on_worker = false;
    bool live = false;
};

struct SnapshotSharedMesh {
    uint64_t mesh_high = 0;
    uint64_t mesh_low = 0;
    uint64_t material_high = 0;
    uint64_t material_low = 0;
    uint32_t placement_index = 0;
    uint32_t lod_level = 0;
    wi::ecs::Entity entity = wi::ecs::INVALID_ENTITY;
    size_t references = 0;
    size_t bytes = 0;
    bool live = false;
};

// A material set gives each material slot of a multi-material mesh its own
// registered material. Sets share the material ID space, so a snapshot row
// names one material for every slot or a set with one material per slot.
struct SnapshotMaterialSet {
    uint64_t high = 0;
    uint64_t low = 0;
    std::array<uint64_t, MAX_SNAPSHOT_MATERIAL_SET_SLOTS> material_high{};
    std::array<uint64_t, MAX_SNAPSHOT_MATERIAL_SET_SLOTS> material_low{};
    uint32_t count = 0;
    bool live = false;
};

struct SnapshotMaterialSets {
    std::array<SnapshotMaterialSet, MAX_SNAPSHOT_MATERIAL_SETS> sets{};
    // The C ABI has no arrays: slots are staged in order, then registered.
    SnapshotMaterialSet staged{};
};

// An asynchronous snapshot asset request. The worker resolves, verifies and
// reads the asset; the owner thread adopts the result into an ordinary asset
// slot when it pumps. A request that fails stays here, holding its status,
// until the ID is unregistered or requested again.
enum class SnapshotAssetKind : int32_t { Mesh = 0, Texture = 1 };

struct SnapshotAssetRequest {
    uint64_t high = 0;
    uint64_t low = 0;
    uint64_t serial = 0;
    SnapshotAssetKind kind = SnapshotAssetKind::Mesh;
    std::string path;
    std::string section;
    int32_t failure = ELISA_RENDER_SCENE_OK;
    bool live = false;
};

// A worker result. The default is a failed load, which is also what a job
// that throws posts.
struct SnapshotAssetLoad {
    int32_t status = ELISA_RENDER_SCENE_ASSET_LOAD_FAILED;
    std::filesystem::path canonical_path;
    elisa::assets::CookedGeometry geometry;
    elisa::assets::LodGeometryChain lod_chain;
    elisa::assets::BundleTexture texture;
    elisa::rendering::textures::DecodedImage decoded_texture;
    bool decoded_on_worker = false;
    bool has_lod_chain = false;
    size_t bytes = 0;
    std::string error;
};

struct SnapshotAssetRequests {
    std::array<SnapshotAssetRequest, MAX_SNAPSHOT_ASSET_REQUESTS> requests{};
    uint64_t next_serial = 1;
    elisa::assets::SerialJobWorker<SnapshotAssetLoad> worker;

    // Joins the worker before the tables it reports into are cleared.
    void reset() {
        worker.stop();
        requests = {};
    }
};
