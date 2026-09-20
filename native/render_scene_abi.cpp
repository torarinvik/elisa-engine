#include "render_scene_abi.h"

#include "application_abi.h"
#include "wiHelper.h"
#include "wiApplication.h"
#include "wiGraphics.h"
#include "wiJobSystem.h"
#include "wiRenderer.h"
#include "wiRenderPath3D.h"
#include "wiScene.h"
#include "wiTrailRenderer.h"
#include "render_cooked_mesh.h"
#include "render_scene_effects.h"
#include "render_scene_textures.h"

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

constexpr size_t MAX_INSTANCES = 256;
constexpr size_t MAX_ELECTRIC_ARCS = 256;
constexpr unsigned HANDLE_SLOT_BITS = 9;
constexpr uint64_t HANDLE_SLOT_MASK = (uint64_t(1) << HANDLE_SLOT_BITS) - 1;
constexpr uint64_t MAX_GENERATION = uint64_t(std::numeric_limits<int64_t>::max()) >> HANDLE_SLOT_BITS;
constexpr int32_t MAX_VIEWPORT = 16384;
constexpr float MAX_SCENE_MAGNITUDE = 1.0e6f;
constexpr float MAX_ORTHOGRAPHIC_HEIGHT = 1.0e6f;
constexpr float MIN_ORTHOGRAPHIC_HEIGHT = 1.0e-3f;
constexpr uint32_t PIPELINE_WAIT_ATTEMPTS = 40;
constexpr float PIPELINE_WAIT_MILLISECONDS = 10.0f;

struct InstanceSlot {
    wi::ecs::Entity entity = wi::ecs::INVALID_ENTITY;
    uint64_t generation = 0;
    std::vector<wi::ecs::Entity> joint_entities;
    std::vector<elisa::assets::CookedGeometry::SkinJoint> skin_joints;
    std::vector<elisa::assets::CookedGeometry::AnimationClip> animation_clips;
    int32_t animation_clip = -1;
    int32_t previous_animation_clip = -1;
    float animation_time = 0.0f;
    float previous_animation_time = 0.0f;
    float animation_speed = 1.0f;
    float previous_animation_speed = 1.0f;
    float blend_elapsed = 0.0f;
    float blend_duration = 0.0f;
    bool animation_loop = true;
    bool previous_animation_loop = true;
    bool live = false;
};

struct ElectricArcSlot {
    wi::TrailRenderer halo;
    wi::TrailRenderer core;
    wi::TrailRenderer branch;
    uint64_t generation = 0;
    uint32_t seed = 0;
    float width = 0.0f;
    float amplitude = 0.0f;
    bool visible = false;
    bool live = false;
};

struct RenderSceneService {
    std::mutex mutex;
    std::unique_ptr<wi::scene::Scene> scene;
    std::unique_ptr<wi::RenderPath3D> path;
    std::array<InstanceSlot, MAX_INSTANCES> instances{};
    std::array<ElectricArcSlot, MAX_ELECTRIC_ARCS> electric_arcs{};
    wi::ecs::Entity camera_entity = wi::ecs::INVALID_ENTITY;
    wi::scene::CameraComponent* camera = nullptr;
    std::thread::id owner_thread{};
    int32_t width = 0;
    int32_t height = 0;
    float vertical_size = 10.0f;
    float eye[3] = {0.0f, 15.0f, 0.0f};
    float target[3] = {0.0f, 0.0f, 0.0f};
    float up[3] = {0.0f, 0.0f, -1.0f};
    bool initialized = false;
    bool shutdown_hook_registered = false;
};

class ElisaRenderPath3D final : public wi::RenderPath3D {
public:
    explicit ElisaRenderPath3D(const std::array<ElectricArcSlot, MAX_ELECTRIC_ARCS>* arcs)
        : arcs_(arcs) {}

    void Render() const override {
        const bool debug_was_enabled = wi::renderer::IsDebugDrawEnabled();
        bool has_live_arc = false;
        if (arcs_ != nullptr) {
            for (const ElectricArcSlot& arc : *arcs_) {
                if (arc.live && arc.visible) {
                    has_live_arc = true;
                    break;
                }
            }
        }
        if (has_live_arc && !debug_was_enabled) wi::renderer::SetDebugDrawEnabled(true);
        if (has_live_arc && arcs_ != nullptr) {
            for (const ElectricArcSlot& arc : *arcs_) {
                if (arc.live && arc.visible) {
                    wi::renderer::DrawTrail(&arc.halo);
                    wi::renderer::DrawTrail(&arc.core);
                    if (arc.branch.points.size() >= 2) wi::renderer::DrawTrail(&arc.branch);
                }
            }
        }
        wi::RenderPath3D::Render();
        if (has_live_arc && !debug_was_enabled) wi::renderer::SetDebugDrawEnabled(false);
    }

private:
    const std::array<ElectricArcSlot, MAX_ELECTRIC_ARCS>* arcs_;
};

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

void apply_camera_look_at(RenderSceneService& state) {
    const XMVECTOR eye = XMVectorSet(state.eye[0], state.eye[1], state.eye[2], 1.0f);
    const XMVECTOR target = XMVectorSet(state.target[0], state.target[1], state.target[2], 1.0f);
    const XMVECTOR up = XMVectorSet(state.up[0], state.up[1], state.up[2], 0.0f);
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
    const double length = std::sqrt(double(qx) * qx + double(qy) * qy +
        double(qz) * qz + double(qw) * qw);
    const float inverse_length = float(1.0 / length);
    transform.translation_local = XMFLOAT3(px, py, pz);
    transform.rotation_local = XMFLOAT4(qx * inverse_length, qy * inverse_length,
        qz * inverse_length, qw * inverse_length);
    transform.scale_local = XMFLOAT3(sx, sy, sz);
    transform.SetDirty();
    transform.UpdateTransform();
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

size_t decode_arc_handle(const RenderSceneService& state, int64_t handle) {
    if (handle <= 0) return MAX_ELECTRIC_ARCS;
    const uint64_t value = uint64_t(handle);
    const uint64_t encoded_slot = value & HANDLE_SLOT_MASK;
    if (encoded_slot == 0 || encoded_slot > MAX_ELECTRIC_ARCS) return MAX_ELECTRIC_ARCS;
    const size_t slot = size_t(encoded_slot - 1);
    const uint64_t generation = value >> HANDLE_SLOT_BITS;
    const ElectricArcSlot& arc = state.electric_arcs[slot];
    return generation != 0 && arc.live && arc.generation == generation ? slot : MAX_ELECTRIC_ARCS;
}

#include "render_scene_animation_internal.inc"

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

void reset_unlocked(RenderSceneService& state) {
    if (state.initialized) {
        wi::jobsystem::WaitForAllJobs();
        if (wi::graphics::GetDevice() != nullptr) wi::graphics::GetDevice()->WaitForGPU();
    }
    if (state.path != nullptr) {
        state.path->scene = nullptr;
        state.path->camera = nullptr;
        state.path.reset();
    }
    if (state.scene != nullptr) {
        state.scene->Clear();
        state.scene.reset();
    }
    for (InstanceSlot& instance : state.instances) {
        instance.entity = wi::ecs::INVALID_ENTITY;
        instance.joint_entities.clear();
        instance.skin_joints.clear();
        instance.animation_clips.clear();
        clear_animation_state(instance);
        instance.live = false;
    }
    for (ElectricArcSlot& arc : state.electric_arcs) {
        arc.halo.Clear();
        arc.core.Clear();
        arc.branch.Clear();
        arc.visible = false;
        arc.live = false;
    }
    state.camera_entity = wi::ecs::INVALID_ENTITY;
    state.camera = nullptr;
    state.owner_thread = std::thread::id{};
    state.width = 0;
    state.height = 0;
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
    state.camera->CreateOrtho(float(width), float(height), 0.01f, 1000.0f, state.vertical_size);
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

} // namespace

extern "C" uint32_t elisa_render_scene_abi_version(void) {
    return ELISA_RENDER_SCENE_ABI_VERSION;
}

extern "C" int32_t elisa_render_scene_v1_initialize(
    int32_t width, int32_t height, float vertical_size) {
    if (!valid_viewport(width, height) || !finite(vertical_size) ||
        vertical_size < MIN_ORTHOGRAPHIC_HEIGHT || vertical_size > MAX_ORTHOGRAPHIC_HEIGHT) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    const int32_t application_status = elisa_application_v1_validate_owner_thread();
    if (application_status == ELISA_APPLICATION_INVALID_STATE) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (application_status == ELISA_APPLICATION_WRONG_THREAD) return ELISA_RENDER_SCENE_WRONG_THREAD;
    if (application_status != ELISA_APPLICATION_OK) return ELISA_RENDER_SCENE_BACKEND_FAILED;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (state.initialized) return ELISA_RENDER_SCENE_ALREADY_INITIALIZED;
    if (wi::graphics::GetDevice() == nullptr) return ELISA_RENDER_SCENE_NOT_INITIALIZED;

    try {
        state.scene = std::make_unique<wi::scene::Scene>();
        state.path = std::make_unique<ElisaRenderPath3D>(&state.electric_arcs);
    } catch (...) {
        reset_unlocked(state);
        return ELISA_RENDER_SCENE_BACKEND_FAILED;
    }

    state.owner_thread = std::this_thread::get_id();
    state.vertical_size = vertical_size;
    state.camera_entity = state.scene->Entity_CreateCamera("elisa_runtime_camera", float(width), float(height));
    state.camera = state.scene->cameras.GetComponent(state.camera_entity);
    wi::scene::TransformComponent* camera_transform = state.scene->transforms.GetComponent(state.camera_entity);
    if (state.camera_entity == wi::ecs::INVALID_ENTITY || state.camera == nullptr || camera_transform == nullptr) {
        reset_unlocked(state);
        return ELISA_RENDER_SCENE_BACKEND_FAILED;
    }

    state.camera->CreateOrtho(float(width), float(height), 0.01f, 1000.0f, vertical_size);
    state.eye[0] = 0.0f;
    state.eye[1] = vertical_size * 1.5f;
    state.eye[2] = 0.0f;
    state.target[0] = state.target[1] = state.target[2] = 0.0f;
    state.up[0] = 0.0f;
    state.up[1] = 0.0f;
    state.up[2] = -1.0f;
    apply_camera_look_at(state);
    state.path->scene = state.scene.get();
    state.path->camera = state.camera;
    state.path->setOcclusionCullingEnabled(false);

    if (!state.shutdown_hook_registered) {
        if (elisa_application_v1_register_shutdown_hook(&state, on_application_shutdown) != ELISA_APPLICATION_OK) {
            reset_unlocked(state);
            return ELISA_RENDER_SCENE_BACKEND_FAILED;
        }
        state.shutdown_hook_registered = true;
    }
    state.initialized = true;
    if (elisa_application_v1_activate_render_path(state.path.get()) != ELISA_APPLICATION_OK) {
        reset_unlocked(state);
        return ELISA_RENDER_SCENE_BACKEND_FAILED;
    }
    state.width = width;
    state.height = height;
    return ELISA_RENDER_SCENE_OK;
}

extern "C" int32_t elisa_render_scene_v1_resize(int32_t width, int32_t height) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    return resize_unlocked(state, width, height);
}

extern "C" int32_t elisa_render_scene_v1_set_camera_look_at(
    float eye_x, float eye_y, float eye_z,
    float target_x, float target_y, float target_z,
    float up_x, float up_y, float up_z) {
    if (!valid_look_at(eye_x, eye_y, eye_z, target_x, target_y, target_z, up_x, up_y, up_z)) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    state.eye[0] = eye_x; state.eye[1] = eye_y; state.eye[2] = eye_z;
    state.target[0] = target_x; state.target[1] = target_y; state.target[2] = target_z;
    state.up[0] = up_x; state.up[1] = up_y; state.up[2] = up_z;
    apply_camera_look_at(state);
    return ELISA_RENDER_SCENE_OK;
}

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
    instance.joint_entities.clear();
    instance.skin_joints.clear();
    instance.animation_clips.clear();
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

#include "render_scene_animation_abi.inc"

#include "render_scene_material_abi.inc"
extern "C" int32_t elisa_render_scene_v1_set_bloom(int32_t enabled, float threshold) {
    if ((enabled != 0 && enabled != 1) || !finite(threshold) || threshold < 0.0f ||
        threshold > elisa::render_scene_effects::MAX_BLOOM_THRESHOLD) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    elisa::render_scene_effects::apply_bloom(*state.path, enabled != 0, threshold);
    return ELISA_RENDER_SCENE_OK;
}
extern "C" int32_t elisa_render_scene_v1_set_visible(int64_t handle, int32_t visible) {
    if (visible != 0 && visible != 1) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    size_t slot = MAX_INSTANCES;
    if (!valid_handle(state, handle, slot)) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    wi::scene::ObjectComponent* object = state.scene->objects.GetComponent(state.instances[slot].entity);
    if (object == nullptr) return ELISA_RENDER_SCENE_BACKEND_FAILED;
    object->SetRenderable(visible != 0);
    return ELISA_RENDER_SCENE_OK;
}

extern "C" int32_t elisa_render_scene_v1_destroy(int64_t handle) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    size_t slot = MAX_INSTANCES;
    if (!valid_handle(state, handle, slot)) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    state.scene->Entity_Remove(state.instances[slot].entity);
    state.instances[slot].entity = wi::ecs::INVALID_ENTITY;
    state.instances[slot].joint_entities.clear();
    state.instances[slot].skin_joints.clear();
    state.instances[slot].animation_clips.clear();
    clear_animation_state(state.instances[slot]);
    state.instances[slot].live = false;
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
#include "render_scene_pixel_probe.h"
#endif
