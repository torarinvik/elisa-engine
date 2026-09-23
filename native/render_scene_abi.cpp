#include "render_scene_abi.h"
#include "application_abi.h"
#include "wiHelper.h"
#include "wiApplication.h"
#include "wiGraphics.h"
#include "wiJobSystem.h"
#include "wiRenderer.h"
#include "wiRenderPath3D.h"
#include "wiScene.h"
#include "wiSprite.h"
#include "wiSpriteFont.h"
#include "wiTextureHelper.h"
#include "wiTrailRenderer.h"
#include "render_cooked_mesh.h"
#include "render_scene_effects.h"
#include "postprocess_bridge.h"
#include "lighting_bridge.h"
#include "visibility_lod_bridge.h"
#include "animation_submission_bridge.h"
#include "effect_bridge.h"
#include "debug_draw_bridge.h"
#include "picking_bridge.h"
#include "selection_outline_bridge.h"
#include "render_scene_textures.h"
#include "lod_geometry_chain.h"
#include "lod_selection.h"
#include "bundle_texture.h"
#include "snapshot_asset_worker.h"
#include <DirectXMath.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
namespace {
constexpr size_t MAX_INSTANCES = 512;
// An instance or snapshot row without a shared snapshot mesh.
constexpr size_t NO_SHARED_MESH = std::numeric_limits<size_t>::max();
constexpr size_t MAX_ELECTRIC_ARCS = 1024;
constexpr unsigned HANDLE_SLOT_BITS = 11;
constexpr uint64_t HANDLE_SLOT_MASK = (uint64_t(1) << HANDLE_SLOT_BITS) - 1;
constexpr uint64_t MAX_GENERATION = uint64_t(std::numeric_limits<int64_t>::max()) >> HANDLE_SLOT_BITS;
constexpr int32_t MAX_VIEWPORT = 16384;
constexpr float MAX_SCENE_MAGNITUDE = 1.0e6f;
constexpr float MAX_ORTHOGRAPHIC_HEIGHT = 1.0e6f;
constexpr float MIN_ORTHOGRAPHIC_HEIGHT = 1.0e-3f;
constexpr float MIN_CAMERA_FOV_RADIANS = 0.01745329252f;
constexpr float MAX_CAMERA_FOV_RADIANS = 3.124139361f;
constexpr float MIN_CAMERA_CLIP_DISTANCE = 1.0e-4f;
constexpr float DEFAULT_CAMERA_NEAR_CLIP = 0.01f;
constexpr float DEFAULT_CAMERA_FAR_CLIP = 1000.0f;
constexpr float DEFAULT_CAMERA_FOV_RADIANS = XM_PIDIV4;
constexpr size_t MAX_RENDER_LIGHTS = probe::LightingBridge::MAX_LIGHTS;
constexpr size_t MAX_RENDER_CAMERAS = 8;
constexpr unsigned CAMERA_HANDLE_SLOT_BITS = 4;
constexpr uint64_t CAMERA_HANDLE_SLOT_MASK = (uint64_t(1) << CAMERA_HANDLE_SLOT_BITS) - 1;
#include "render_scene_text_internal.inc"
#include "render_scene_panel_internal.inc"
#include "render_scene_instance_state.inc"
struct RenderLightSlot {
    probe::NativeLightHandle native{};
    bool live = false;
};
struct RenderCameraSlot {
    wi::ecs::Entity entity = wi::ecs::INVALID_ENTITY;
    uint64_t generation = 0;
    bool live = false;
};
#include "render_scene_snapshot_state.inc"
struct ElectricArcSlot {
    wi::TrailRenderer halo;
    wi::TrailRenderer core;
    wi::TrailRenderer branch;
    uint64_t generation = 0;
    uint32_t seed = 0;
    float width = 0.0f;
    float amplitude = 0.0f;
    bool depth_test = false;
    bool visible = false;
    bool live = false;
};
struct RenderSceneService {
    std::mutex mutex;
    std::unique_ptr<wi::scene::Scene> scene;
    std::unique_ptr<wi::RenderPath3D> path;
    std::array<InstanceSlot, MAX_INSTANCES> instances{};
    std::array<RenderLightSlot, MAX_RENDER_LIGHTS> lights{};
    std::array<RenderCameraSlot, MAX_RENDER_CAMERAS> cameras{};
    std::array<ElectricArcSlot, MAX_ELECTRIC_ARCS> electric_arcs{};
    std::array<OverlayTextSlot, MAX_OVERLAY_TEXTS> overlay_texts{};
    std::array<OverlayPanelSlot, MAX_OVERLAY_PANELS> overlay_panels{};
    std::array<OverlayImageSlot, MAX_OVERLAY_IMAGES> overlay_images{}; probe::DebugDrawBridge debug_draw;
    std::array<SnapshotStageRow, MAX_INSTANCES> snapshot_rows{};
    std::array<int64_t, MAX_INSTANCES> snapshot_retire_handles{};
    std::array<int64_t, MAX_INSTANCES> snapshot_results{};
    std::array<SnapshotMeshAssetSlot, MAX_SNAPSHOT_MESH_ASSETS> snapshot_mesh_assets{};
    std::array<SnapshotMaterialAssetSlot, MAX_SNAPSHOT_MATERIAL_ASSETS> snapshot_material_assets{};
    SnapshotMaterialSets snapshot_material_sets;
    std::array<SnapshotTextureAssetSlot, MAX_SNAPSHOT_TEXTURE_ASSETS> snapshot_texture_assets{};
    std::array<SnapshotSharedMesh, MAX_SNAPSHOT_SHARED_MESHES> snapshot_shared_meshes{};
    size_t snapshot_geometry_bytes = 0;
    size_t snapshot_geometry_budget = MAX_SNAPSHOT_GEOMETRY_BYTES; uint64_t snapshot_asset_use_clock = 0;
    size_t snapshot_shared_geometry_bytes = 0;
    size_t snapshot_bundle_texture_source_bytes = 0; size_t snapshot_bundle_texture_source_budget = MAX_SNAPSHOT_TEXTURE_SOURCE_BYTES;
    size_t snapshot_decoded_texture_bytes = 0;
    size_t snapshot_decoded_texture_budget = MAX_SNAPSHOT_DECODED_TEXTURE_BYTES; uint64_t snapshot_texture_use_clock = 0;
    SnapshotAssetRequests snapshot_asset_requests;
#if defined(ELISA_RENDER_SCENE_TEST_PROBE)
    int64_t arc_depth_test_probe_handle = 0;
    uint64_t snapshot_test_transaction_api_calls = 0;
    uint64_t snapshot_test_last_transaction_api_calls = 0;
#endif
    size_t snapshot_row_count = 0;
    size_t snapshot_retire_count = 0;
    size_t snapshot_result_count = 0;
    size_t snapshot_expected_previous_count = 0;
    bool snapshot_transaction_active = false;
    int32_t snapshot_test_fail_after_creates = -1;
    std::unique_ptr<probe::LightingBridge> lighting;
    std::unique_ptr<probe::VisibilityLodBridge> visibility_lod;
    std::unique_ptr<probe::AnimationSubmissionBridge> animation_submission;
    std::unique_ptr<probe::EffectBridge> effects;
    std::unique_ptr<probe::PickingBridge> picking;
    std::unique_ptr<probe::SelectionOutlineBridge> selection;
    wi::ecs::Entity sun_entity = wi::ecs::INVALID_ENTITY;
    wi::ecs::Entity camera_entity = wi::ecs::INVALID_ENTITY;
    wi::ecs::Entity primary_camera_entity = wi::ecs::INVALID_ENTITY;
    wi::scene::CameraComponent* camera = nullptr;
    std::thread::id owner_thread{};
    int32_t width = 0;
    int32_t height = 0;
    float vertical_size = 10.0f;
    float perspective_fov = DEFAULT_CAMERA_FOV_RADIANS;
    float camera_near_clip = DEFAULT_CAMERA_NEAR_CLIP;
    float camera_far_clip = DEFAULT_CAMERA_FAR_CLIP;
    bool perspective_camera = false;
    int32_t shadow_quality = 1;
    float eye[3] = {0.0f, 15.0f, 0.0f};
    float target[3] = {0.0f, 0.0f, 0.0f};
    float up[3] = {0.0f, 0.0f, -1.0f};
    bool initialized = false;
    bool shutdown_hook_registered = false;
};
void update_snapshot_lod_selection(RenderSceneService& state);
#include "render_scene_path.inc"
RenderSceneService& service() {
    // NativeApplication owns a static host and runs registered hooks while
    // that host is being destroyed. Keep the callback context alive until
    // process exit even if C++ static destruction order runs this service
    // before the host.
    static RenderSceneService* value = new RenderSceneService();
    return *value;
}
bool finite(float value) {
    return std::isfinite(value);
}
bool bounded(float value, float magnitude = MAX_SCENE_MAGNITUDE) {
    return finite(value) && std::abs(value) <= magnitude;
}
bool valid_viewport(int32_t width, int32_t height) {
    return width > 0 && height > 0 && width <= MAX_VIEWPORT && height <= MAX_VIEWPORT;
}
bool valid_color(float red, float green, float blue, float alpha) {
    return finite(red) && finite(green) && finite(blue) && finite(alpha) &&
        red >= 0.0f && red <= 1.0f && green >= 0.0f && green <= 1.0f &&
        blue >= 0.0f && blue <= 1.0f && alpha >= 0.0f && alpha <= 1.0f;
}
#include "render_scene_text_helpers.inc"
#include "render_scene_panel_helpers.inc"
#include "render_scene_overlay_reset.inc"
bool on_owner_thread(const RenderSceneService& state) {
    return state.owner_thread == std::this_thread::get_id();
}
bool valid_transform(
    float px, float py, float pz,
    float qx, float qy, float qz, float qw,
    float sx, float sy, float sz) {
    if (!bounded(px) || !bounded(py) || !bounded(pz) || !bounded(qx) || !bounded(qy) ||
        !bounded(qz) || !bounded(qw) || !finite(sx) || !finite(sy) || !finite(sz) ||
        sx <= 0.0f || sy <= 0.0f || sz <= 0.0f ||
        sx > MAX_SCENE_MAGNITUDE || sy > MAX_SCENE_MAGNITUDE || sz > MAX_SCENE_MAGNITUDE) return false;
    const double length_squared = double(qx) * qx + double(qy) * qy +
        double(qz) * qz + double(qw) * qw;
    return std::isfinite(length_squared) && length_squared > 1.0e-12;
}
bool valid_look_at(
    float eye_x, float eye_y, float eye_z,
    float target_x, float target_y, float target_z,
    float up_x, float up_y, float up_z) {
    if (!bounded(eye_x) || !bounded(eye_y) || !bounded(eye_z) ||
        !bounded(target_x) || !bounded(target_y) || !bounded(target_z) ||
        !bounded(up_x) || !bounded(up_y) || !bounded(up_z)) return false;
    const double dx = double(target_x) - eye_x;
    const double dy = double(target_y) - eye_y;
    const double dz = double(target_z) - eye_z;
    const double direction_squared = dx * dx + dy * dy + dz * dz;
    const double up_squared = double(up_x) * up_x + double(up_y) * up_y + double(up_z) * up_z;
    const double cross_x = dy * up_z - dz * up_y;
    const double cross_y = dz * up_x - dx * up_z;
    const double cross_z = dx * up_y - dy * up_x;
    const double cross_squared = cross_x * cross_x + cross_y * cross_y + cross_z * cross_z;
    return std::isfinite(direction_squared) && std::isfinite(up_squared) && std::isfinite(cross_squared) &&
        direction_squared > 1.0e-8 && up_squared > 1.0e-8 && cross_squared > 1.0e-8;
}
// The look-at stays in Elisa space in `state` and is reflected into Wicked's
// left-handed frame here, like every object, so world +X shows on the right
// of a right-handed camera.
void apply_camera_look_at(RenderSceneService& state) {
    const XMFLOAT3 wicked_eye = probe::coordinates::to_wicked(state.eye[0], state.eye[1], state.eye[2]);
    const XMFLOAT3 wicked_target = probe::coordinates::to_wicked(state.target[0], state.target[1], state.target[2]);
    const XMFLOAT3 wicked_up = probe::coordinates::to_wicked_direction(
        XMFLOAT3(state.up[0], state.up[1], state.up[2]));
    const XMVECTOR eye = XMVectorSet(wicked_eye.x, wicked_eye.y, wicked_eye.z, 1.0f);
    const XMVECTOR target = XMVectorSet(wicked_target.x, wicked_target.y, wicked_target.z, 1.0f);
    const XMVECTOR up = XMVectorSet(wicked_up.x, wicked_up.y, wicked_up.z, 0.0f);
    const XMMATRIX world = XMMatrixInverse(nullptr, XMMatrixLookAtLH(eye, target, up));
    wi::scene::TransformComponent* transform = state.scene->transforms.GetComponent(state.camera_entity);
    if (transform != nullptr) {
        XMVECTOR scale;
        XMVECTOR rotation;
        XMVECTOR translation;
        XMMatrixDecompose(&scale, &rotation, &translation, world);
        XMStoreFloat3(&transform->translation_local, translation);
        XMStoreFloat4(&transform->rotation_local, rotation);
        transform->SetDirty();
        transform->UpdateTransform();
        state.camera->TransformCamera(*transform);
    } else {
        state.camera->TransformCamera(world);
    }
    state.camera->UpdateCamera();
}
void apply_transform(wi::scene::TransformComponent& transform,
    float px, float py, float pz,
    float qx, float qy, float qz, float qw,
    float sx, float sy, float sz) {
    elisa::rendering::set_transform(transform, px, py, pz, qx, qy, qz, qw, sx, sy, sz);
}
uint64_t encode_handle(size_t slot, uint64_t generation) {
    return (generation << HANDLE_SLOT_BITS) | uint64_t(slot + 1);
}
size_t decode_handle(uint64_t value) {
    const uint64_t encoded_slot = value & HANDLE_SLOT_MASK;
    if (encoded_slot == 0 || encoded_slot > MAX_INSTANCES) return MAX_INSTANCES;
    return size_t(encoded_slot - 1);
}
bool valid_handle(const RenderSceneService& state, int64_t handle, size_t& slot) {
    if (handle <= 0) return false;
    const uint64_t value = uint64_t(handle);
    slot = decode_handle(value);
    if (slot >= MAX_INSTANCES) return false;
    const uint64_t generation = value >> HANDLE_SLOT_BITS;
    return generation != 0 && state.instances[slot].live &&
        state.instances[slot].generation == generation;
}
int32_t resize_unlocked(RenderSceneService& state, int32_t width, int32_t height);
void release_animation_submission(RenderSceneService& state, InstanceSlot& instance) {
    if (state.animation_submission != nullptr && instance.animation_submission.owner != 0) {
        if (state.animation_submission->pending(instance.animation_submission)) {
            state.animation_submission->complete(instance.animation_submission);
        }
        state.animation_submission->destroy(instance.animation_submission);
    }
    instance.animation_submission = {};
}
#include "render_scene_imported_scene_internal.inc"
#include "render_scene_animation_internal.inc"
#include "render_scene_selection_internal.inc"
size_t find_free_slot(const RenderSceneService& state) {
    for (size_t index = 0; index < MAX_INSTANCES; ++index) {
        if (!state.instances[index].live) return index;
    }
    return MAX_INSTANCES;
}
size_t find_free_arc_slot(const RenderSceneService& state) {
    for (size_t index = 0; index < MAX_ELECTRIC_ARCS; ++index) {
        if (!state.electric_arcs[index].live) return index;
    }
    return MAX_ELECTRIC_ARCS;
}
void clear_snapshot_instance(RenderSceneService& state, InstanceSlot& instance);
void reset_unlocked(RenderSceneService& state) {
    if (state.initialized) {
        wi::jobsystem::WaitForAllJobs();
        if (wi::graphics::GetDevice() != nullptr) wi::graphics::GetDevice()->WaitForGPU();
    }
    if (state.selection != nullptr) state.selection->clear();
    for (InstanceSlot& instance : state.instances) clear_instance_pick_bindings(state, instance);
    state.selection.reset();
    state.picking.reset();
    if (state.path != nullptr) {
        state.path->ClearFonts();
        state.path->scene = nullptr;
        state.path->camera = nullptr;
        state.path.reset();
    }
    state.lighting.reset();
    state.visibility_lod.reset();
    state.effects.reset();
    for (InstanceSlot& instance : state.instances) {
        release_animation_submission(state, instance);
    }
    state.animation_submission.reset();
    if (state.scene != nullptr) {
        state.scene->Clear();
        state.scene.reset();
    }
    state.lights = {};
    state.cameras = {};
    for (InstanceSlot& instance : state.instances) clear_snapshot_instance(state, instance);
    state.snapshot_row_count = 0;
    state.snapshot_retire_count = 0;
    state.snapshot_result_count = 0;
    state.snapshot_expected_previous_count = 0;
    state.snapshot_transaction_active = false;
    state.snapshot_test_fail_after_creates = -1;
    state.snapshot_asset_requests.reset(); state.debug_draw.clear();
#if defined(ELISA_RENDER_SCENE_TEST_PROBE)
    state.snapshot_test_transaction_api_calls = 0;
    state.snapshot_test_last_transaction_api_calls = 0;
#endif
    state.snapshot_rows = {};
    state.snapshot_retire_handles = {};
    state.snapshot_results = {};
    state.snapshot_mesh_assets = {};
    state.snapshot_material_assets = {};
    state.snapshot_material_sets = {};
    state.snapshot_texture_assets = {};
    state.snapshot_shared_meshes = {};
    state.snapshot_geometry_bytes = 0;
    state.snapshot_geometry_budget = MAX_SNAPSHOT_GEOMETRY_BYTES; state.snapshot_asset_use_clock = 0;
    state.snapshot_shared_geometry_bytes = 0;
    state.snapshot_bundle_texture_source_bytes = 0; state.snapshot_bundle_texture_source_budget = MAX_SNAPSHOT_TEXTURE_SOURCE_BYTES;
    state.snapshot_decoded_texture_bytes = 0;
    state.snapshot_decoded_texture_budget = MAX_SNAPSHOT_DECODED_TEXTURE_BYTES; state.snapshot_texture_use_clock = 0;
    state.sun_entity = wi::ecs::INVALID_ENTITY;
    for (ElectricArcSlot& arc : state.electric_arcs) {
        arc.halo.Clear();
        arc.core.Clear();
        arc.branch.Clear();
        arc.visible = false;
        arc.live = false;
    }
    reset_overlay_slots(state);
    state.camera_entity = wi::ecs::INVALID_ENTITY;
    state.primary_camera_entity = wi::ecs::INVALID_ENTITY;
    state.camera = nullptr;
    state.owner_thread = std::thread::id{};
    state.width = 0;
    state.height = 0;
    state.vertical_size = 10.0f;
    state.perspective_fov = DEFAULT_CAMERA_FOV_RADIANS;
    state.camera_near_clip = DEFAULT_CAMERA_NEAR_CLIP;
    state.camera_far_clip = DEFAULT_CAMERA_FAR_CLIP;
    state.perspective_camera = false;
    state.shadow_quality = 1;
    state.initialized = false;
}
void on_application_shutdown(void* context) {
    auto* state = static_cast<RenderSceneService*>(context);
    if (state == nullptr) return;
    int stage = 0;
    try {
        std::lock_guard<std::mutex> guard(state->mutex);
        // NativeApplication detaches its active path before running service hooks.
        // Releasing the scene here keeps all Wicked resources ahead of device teardown.
        stage = 1;
        reset_unlocked(*state);
        stage = 2;
        state->shutdown_hook_registered = false;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "render scene shutdown exception at stage %d: %s\n", stage, error.what());
    } catch (...) {
        std::fprintf(stderr, "render scene shutdown exception at stage %d\n", stage);
    }
}
int32_t resize_unlocked(RenderSceneService& state, int32_t width, int32_t height) {
    if (!valid_viewport(width, height)) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    if (state.camera == nullptr) return ELISA_RENDER_SCENE_BACKEND_FAILED;
    if (state.perspective_camera) {
        state.camera->CreatePerspective(float(width), float(height),
            state.camera_near_clip, state.camera_far_clip, state.perspective_fov);
    } else {
        state.camera->CreateOrtho(float(width), float(height),
            state.camera_near_clip, state.camera_far_clip, state.vertical_size);
    }
    apply_camera_look_at(state);
    state.width = width;
    state.height = height;
    return ELISA_RENDER_SCENE_OK;
}
int32_t update_transform_unlocked(RenderSceneService& state, size_t slot,
    float px, float py, float pz,
    float qx, float qy, float qz, float qw,
    float sx, float sy, float sz) {
    if (!valid_transform(px, py, pz, qx, qy, qz, qw, sx, sy, sz)) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    wi::scene::TransformComponent* transform = state.scene->transforms.GetComponent(state.instances[slot].entity);
    if (transform == nullptr) return ELISA_RENDER_SCENE_BACKEND_FAILED;
    apply_transform(*transform, px, py, pz, qx, qy, qz, qw, sx, sy, sz);
    return ELISA_RENDER_SCENE_OK;
}
#include "render_scene_snapshot_internal.inc"
} // namespace
extern "C" uint32_t elisa_render_scene_abi_version(void) {
    return ELISA_RENDER_SCENE_ABI_VERSION;
}
#include "render_scene_initialize_abi.inc"
#include "render_scene_camera_abi.inc"
#include "render_scene_camera_handles_abi.inc"
extern "C" int64_t elisa_render_scene_v1_create(
    int32_t primitive,
    float px, float py, float pz,
    float qx, float qy, float qz, float qw,
    float sx, float sy, float sz,
    float red, float green, float blue, float alpha) {
    if ((primitive != ELISA_RENDER_PRIMITIVE_BOX && primitive != ELISA_RENDER_PRIMITIVE_SPHERE &&
        primitive != ELISA_RENDER_PRIMITIVE_PLANE) ||
        !valid_transform(px, py, pz, qx, qy, qz, qw, sx, sy, sz) ||
        !valid_color(red, green, blue, alpha)) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    const size_t slot = find_free_slot(state);
    if (slot == MAX_INSTANCES) return ELISA_RENDER_SCENE_CAPACITY;
    InstanceSlot& instance = state.instances[slot];
    if (instance.generation >= MAX_GENERATION) return ELISA_RENDER_SCENE_GENERATION_EXHAUSTED;
    instance.gameplay_epoch = 0;
    instance.gameplay_id = 0;
    instance.render_id = 0;
    instance.mesh_high = 0;
    instance.mesh_low = 0;
    instance.material_high = 0;
    instance.material_low = 0;
    instance.shared_mesh_slot = NO_SHARED_MESH;
    instance.joint_entities.clear();
    instance.imported_mesh_entities.clear();
    instance.imported_camera_entities.clear();
    instance.imported_light_handles.clear();
    instance.skin_joints.clear();
    instance.animation_clips.clear();
    instance.morph_default_weights.clear();
    instance.animation_submission = {};
    clear_animation_state(instance);
    const uint64_t generation = instance.generation + 1;
    wi::ecs::Entity entity = wi::ecs::INVALID_ENTITY;
    const std::string name = "elisa_primitive_" + std::to_string(slot) + "_" + std::to_string(generation);
    try {
        if (primitive == ELISA_RENDER_PRIMITIVE_BOX) entity = state.scene->Entity_CreateCube(name);
        else if (primitive == ELISA_RENDER_PRIMITIVE_SPHERE) entity = state.scene->Entity_CreateSphere(name, 0.5f, 24, 24);
        else entity = state.scene->Entity_CreatePlane(name);
    } catch (...) {
        return ELISA_RENDER_SCENE_BACKEND_FAILED;
    }
    if (entity == wi::ecs::INVALID_ENTITY) return ELISA_RENDER_SCENE_BACKEND_FAILED;
    wi::scene::TransformComponent* transform = state.scene->transforms.GetComponent(entity);
    wi::scene::MaterialComponent* material = state.scene->materials.GetComponent(entity);
    wi::scene::ObjectComponent* object = state.scene->objects.GetComponent(entity);
    if (transform == nullptr || material == nullptr || object == nullptr) {
        state.scene->Entity_Remove(entity);
        return ELISA_RENDER_SCENE_BACKEND_FAILED;
    }
    apply_transform(*transform, px, py, pz, qx, qy, qz, qw, sx, sy, sz);
    material->shaderType = wi::scene::MaterialComponent::SHADERTYPE_UNLIT;
    material->SetBaseColor(XMFLOAT4(red, green, blue, alpha));
    material->SetCastShadow(false);
    material->userBlendMode = alpha < 0.999f ? wi::enums::BLENDMODE_ALPHA : wi::enums::BLENDMODE_OPAQUE;
    object->SetCastShadow(false);
    instance.entity = entity;
    instance.generation = generation;
    instance.live = true;
    return int64_t(encode_handle(slot, generation));
}
#include "render_scene_mesh_abi.inc"
#include "render_scene_imported_scene_abi.inc"
#include "render_scene_arcs_abi.inc"
extern "C" int32_t elisa_render_scene_v1_update_transform(
    int64_t handle,
    float px, float py, float pz,
    float qx, float qy, float qz, float qw,
    float sx, float sy, float sz) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    size_t slot = MAX_INSTANCES;
    if (!valid_handle(state, handle, slot)) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    return update_transform_unlocked(state, slot, px, py, pz, qx, qy, qz, qw, sx, sy, sz);
}
#include "render_scene_snapshot_abi.inc"
#include "render_scene_snapshot_mesh_scene_abi.inc"
#include "render_scene_snapshot_tint_abi.inc"
#include "render_scene_snapshot_bundle_texture_abi.inc"
#include "render_scene_snapshot_asset_request_abi.inc"
#include "render_scene_animation_abi.inc"
#include "render_scene_animation_submission_abi.inc"
#include "render_scene_effects_abi.inc"
#include "render_scene_material_abi.inc"
#include "render_scene_environment_abi.inc"
#include "render_scene_lighting_abi.inc"
#include "render_scene_visibility_abi.inc"
#include "render_scene_quality_abi.inc"
#include "render_scene_text_abi.inc"
#include "render_scene_panel_abi.inc"
#include "render_scene_selection_abi.inc"
extern "C" int32_t elisa_render_scene_v1_destroy(int64_t handle) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    size_t slot = MAX_INSTANCES;
    if (!valid_handle(state, handle, slot)) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    if (state.selection != nullptr && state.selection->selected(state.instances[slot].entity)) {
        state.selection->clear();
    }
    clear_instance_pick_bindings(state, state.instances[slot]);
    release_animation_submission(state, state.instances[slot]);
    release_imported_scene(state, state.instances[slot]);
    for (wi::ecs::Entity placement : state.instances[slot].snapshot_placement_entities) {
        state.scene->Entity_Remove(placement);
    }
    remove_instance_entity(state, slot);
    for (wi::ecs::Entity joint : state.instances[slot].joint_entities) state.scene->Entity_Remove(joint);
    if (state.instances[slot].shared_mesh_slot < MAX_SNAPSHOT_SHARED_MESHES) {
        release_snapshot_shared_mesh(state, state.instances[slot].shared_mesh_slot);
    }
    for (size_t shared_slot : state.instances[slot].snapshot_additional_shared_mesh_slots) {
        release_snapshot_shared_mesh(state, shared_slot);
    }
    clear_snapshot_instance(state, state.instances[slot]);
    return ELISA_RENDER_SCENE_OK;
}
extern "C" int32_t elisa_render_scene_v1_shutdown(void) {
    RenderSceneService& state = service();
    {
        std::lock_guard<std::mutex> guard(state.mutex);
        if (!state.initialized) return ELISA_RENDER_SCENE_OK;
        if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    }
    const int32_t app_result = elisa_application_v1_activate_render_path(nullptr);
    if (app_result == ELISA_APPLICATION_WRONG_THREAD) return ELISA_RENDER_SCENE_WRONG_THREAD;
    std::lock_guard<std::mutex> guard(state.mutex);
    reset_unlocked(state);
    return ELISA_RENDER_SCENE_OK;
}
extern "C" uint64_t elisa_render_scene_v1_instance_count(void) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    uint64_t count = 0;
    for (const InstanceSlot& instance : state.instances) count += instance.live ? 1 : 0;
    return count;
}
extern "C" int32_t elisa_render_scene_v1_is_initialized(void) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    return state.initialized ? 1 : 0;
}
#if defined(ELISA_RENDER_SCENE_TEST_PROBE)
#include "render_scene_imported_scene_probe.inc"
#include "render_scene_pixel_probe.h"
#include "render_scene_environment_probe.h"
#include "render_scene_arc_probe.h"
#endif
