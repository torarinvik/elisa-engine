#pragma once

#include <algorithm>

namespace {

constexpr uint32_t ARC_RENDER_PROBE_RADIUS = 8;
constexpr uint32_t OVERLAY_TEXT_PROBE_WIDTH = 240;
constexpr uint32_t OVERLAY_TEXT_PROBE_HEIGHT = 80;
constexpr uint8_t OVERLAY_TEXT_ALPHA_THRESHOLD = 16;
constexpr uint8_t OVERLAY_TEXT_BRIGHTNESS_THRESHOLD = 80;
constexpr float OVERLAY_PANEL_PROBE_LOGICAL_X = 240.0f;
constexpr float OVERLAY_PANEL_PROBE_LOGICAL_Y = 120.0f;
constexpr uint8_t OVERLAY_PANEL_ALPHA_THRESHOLD = 200;
constexpr uint8_t OVERLAY_PANEL_COLOR_SEPARATION = 80;
constexpr size_t RGBA8_COLOR_CHANNEL_COUNT = 3;
constexpr size_t RGBA8_CHANNEL_COUNT = 4;
constexpr size_t RGBA8_ALPHA_INDEX = 3;
constexpr uint64_t FNV_OFFSET_BASIS = 14695981039346656037ull;
constexpr uint64_t FNV_PRIME = 1099511628211ull;
// Wicked compiles object pipelines on worker threads and skips draws until
// they are ready. Under CPU load that can take seconds after startup.
constexpr uint32_t PIPELINE_WAIT_ATTEMPTS = 3000;
constexpr float PIPELINE_WAIT_MILLISECONDS = 10.0f;

void wait_for_object_pipelines() {
    for (uint32_t attempt = 0; attempt < PIPELINE_WAIT_ATTEMPTS; ++attempt) {
        if (wi::renderer::IsPipelineCreationActive() == 0) return;
        wi::helper::Sleep(PIPELINE_WAIT_MILLISECONDS);
    }
}

} // namespace

extern "C" int32_t elisa_render_scene_v1_test_snapshot_fail_after_creates(int32_t created_count) {
    if (created_count < 0) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    state.snapshot_test_fail_after_creates = created_count;
    return ELISA_RENDER_SCENE_OK;
}

extern "C" int32_t elisa_render_scene_v1_test_snapshot_identity_matches(
    int64_t gameplay_epoch, int64_t gameplay_id, int64_t render_id,
    uint64_t mesh_high, uint64_t mesh_low, uint64_t material_high, uint64_t material_low) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state)) return 0;
    for (const InstanceSlot& instance : state.instances) {
        if (instance.live && instance.render_id == render_id &&
            instance.gameplay_epoch == gameplay_epoch && instance.gameplay_id == gameplay_id &&
            instance.mesh_high == mesh_high && instance.mesh_low == mesh_low &&
            instance.material_high == material_high && instance.material_low == material_low) return 1;
    }
    return 0;
}

extern "C" int32_t elisa_render_scene_v1_test_snapshot_instance_resources(
    int64_t render_id, uint64_t vertex_count, uint64_t index_count,
    float red, float green, float blue, float alpha, float metallic, float roughness) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state) || state.scene == nullptr) return 0;
    for (const InstanceSlot& instance : state.instances) {
        if (!instance.live || instance.render_id != render_id) continue;
        const wi::scene::ObjectComponent* object = state.scene->objects.GetComponent(instance.entity);
        if (object == nullptr) return 0;
        const wi::scene::MeshComponent* mesh = state.scene->meshes.GetComponent(object->meshID);
        if (mesh == nullptr || mesh->subsets.empty()) return 0;
        const wi::scene::MaterialComponent* material =
            state.scene->materials.GetComponent(mesh->subsets.front().materialID);
        if (mesh == nullptr || material == nullptr || mesh->vertex_positions.size() != vertex_count ||
            mesh->indices.size() != index_count || material->shaderType != wi::scene::MaterialComponent::SHADERTYPE_PBR) return 0;
        const auto close = [](float left, float right) { return std::abs(left - right) <= 1.0e-5f; };
        return close(material->baseColor.x, red) && close(material->baseColor.y, green) &&
            close(material->baseColor.z, blue) && close(material->baseColor.w, alpha) &&
            close(material->metalness, metallic) && close(material->roughness, roughness) ? 1 : 0;
    }
    return 0;
}

extern "C" int32_t elisa_render_scene_v1_test_snapshot_instance_tint(int64_t render_id,
    float red, float green, float blue,
    float emissive_red, float emissive_green, float emissive_blue, float emissive_strength) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state) || state.scene == nullptr) return 0;
    for (const InstanceSlot& instance : state.instances) {
        if (!instance.live || instance.render_id != render_id) continue;
        const wi::scene::ObjectComponent* object = state.scene->objects.GetComponent(instance.entity);
        if (object == nullptr) return 0;
        const auto close = [](float left, float right) { return std::abs(left - right) <= 1.0e-5f; };
        return close(object->color.x, red) && close(object->color.y, green) &&
            close(object->color.z, blue) && close(object->color.w, 1.0f) &&
            close(object->emissiveColor.x, emissive_red) && close(object->emissiveColor.y, emissive_green) &&
            close(object->emissiveColor.z, emissive_blue) &&
            close(object->emissiveColor.w, emissive_strength) ? 1 : 0;
    }
    return 0;
}

namespace {

// Returns the live texture in one slot of a registered snapshot material.
const wi::graphics::Texture* snapshot_material_texture(RenderSceneService& state,
    uint64_t material_high, uint64_t material_low, int32_t slot_code) {
    if (!elisa::rendering::textures::valid_slot(slot_code)) return nullptr;
    if (!state.initialized || !on_owner_thread(state) || state.scene == nullptr) return nullptr;
    const size_t slot = snapshot_material_asset_slot(state, material_high, material_low);
    if (slot == MAX_SNAPSHOT_MATERIAL_ASSETS) return nullptr;
    const wi::scene::MaterialComponent* material = state.scene->materials.GetComponent(
        state.snapshot_material_assets[slot].material_entity);
    if (material == nullptr) return nullptr;
    const size_t wicked_slot = elisa::rendering::textures::MATERIAL_SLOTS[static_cast<size_t>(slot_code)];
    const wi::Resource& resource = material->textures[wicked_slot].resource;
    if (!resource.IsValid() || !resource.GetTexture().IsValid()) return nullptr;
    return &resource.GetTexture();
}

} // namespace

extern "C" int32_t elisa_render_scene_v1_test_snapshot_material_texture(
    uint64_t material_high, uint64_t material_low, int32_t slot_code) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    return snapshot_material_texture(state, material_high, material_low, slot_code) != nullptr ? 1 : 0;
}

extern "C" int32_t elisa_render_scene_v1_test_snapshot_material_texture_size(
    uint64_t material_high, uint64_t material_low, int32_t slot_code, uint32_t width, uint32_t height) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    const wi::graphics::Texture* texture = snapshot_material_texture(state, material_high, material_low, slot_code);
    if (texture == nullptr) return 0;
    const wi::graphics::TextureDesc& desc = texture->GetDesc();
    return desc.width == width && desc.height == height ? 1 : 0;
}

// Source-section bytes admitted by the bundle-backed texture budget.
extern "C" uint64_t elisa_render_scene_v1_test_snapshot_bundle_texture_source_bytes(void) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state)) return UINT64_MAX;
    return static_cast<uint64_t>(state.snapshot_bundle_texture_source_bytes);
}

extern "C" int32_t elisa_render_scene_v1_test_snapshot_texture_decoded_on_worker(
        uint64_t high, uint64_t low) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state)) return 0;
    const size_t slot = snapshot_texture_asset_slot(state, high, low);
    if (slot == MAX_SNAPSHOT_TEXTURE_ASSETS) return 0;
    const SnapshotTextureAssetSlot& asset = state.snapshot_texture_assets[slot];
    const size_t pixel_count = static_cast<size_t>(asset.decoded.width) * asset.decoded.height;
    const size_t stored_channels = asset.decoded.source_channels == 3 ? 4u :
        static_cast<size_t>(asset.decoded.source_channels);
    return asset.section.empty() || !asset.decoded_on_worker || asset.source_bytes == 0 ||
        asset.decoded.width == 0 || asset.decoded.height == 0 || stored_channels == 0 ||
        pixel_count > MAX_SNAPSHOT_DECODED_TEXTURE_BYTES / stored_channels ||
        asset.decoded.pixels.size() != pixel_count * stored_channels ? 0 : 1;
}

extern "C" int32_t elisa_render_scene_v1_test_snapshot_meshes_shared(
    int64_t first_render_id, int64_t second_render_id) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state) || state.scene == nullptr) return 0;
    wi::ecs::Entity first_object_entity = wi::ecs::INVALID_ENTITY;
    wi::ecs::Entity second_object_entity = wi::ecs::INVALID_ENTITY;
    wi::ecs::Entity first_mesh = wi::ecs::INVALID_ENTITY;
    wi::ecs::Entity second_mesh = wi::ecs::INVALID_ENTITY;
    XMFLOAT3 first_translation = XMFLOAT3(0, 0, 0);
    XMFLOAT3 second_translation = XMFLOAT3(0, 0, 0);
    for (const InstanceSlot& instance : state.instances) {
        if (!instance.live) continue;
        const wi::scene::ObjectComponent* object = state.scene->objects.GetComponent(instance.entity);
        const wi::scene::TransformComponent* transform = state.scene->transforms.GetComponent(instance.entity);
        if (object == nullptr || transform == nullptr) continue;
        if (instance.render_id == first_render_id) {
            first_object_entity = instance.entity;
            first_mesh = object->meshID;
            first_translation = transform->translation_local;
        }
        if (instance.render_id == second_render_id) {
            second_object_entity = instance.entity;
            second_mesh = object->meshID;
            second_translation = transform->translation_local;
        }
    }
    const bool transforms_differ = std::abs(first_translation.x - second_translation.x) > 1.0e-5f ||
        std::abs(first_translation.y - second_translation.y) > 1.0e-5f ||
        std::abs(first_translation.z - second_translation.z) > 1.0e-5f;
    return first_object_entity != wi::ecs::INVALID_ENTITY && second_object_entity != wi::ecs::INVALID_ENTITY &&
        first_object_entity != second_object_entity && first_mesh != wi::ecs::INVALID_ENTITY &&
        first_mesh == second_mesh && transforms_differ ? 1 : 0;
}

extern "C" uint64_t elisa_render_scene_v1_test_snapshot_shared_mesh_count(void) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state)) return 0;
    uint64_t count = 0;
    for (const SnapshotSharedMesh& mesh : state.snapshot_shared_meshes) count += mesh.live ? 1 : 0;
    return count;
}

extern "C" uint64_t elisa_render_scene_v1_test_snapshot_last_transaction_api_calls(void) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state)) return 0;
    return state.snapshot_test_last_transaction_api_calls;
}

extern "C" int32_t elisa_render_scene_v1_test_snapshot_position_x(
    int64_t render_id, float expected_x) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state) || state.scene == nullptr) return 0;
    for (const InstanceSlot& instance : state.instances) {
        if (!instance.live || instance.render_id != render_id) continue;
        const wi::scene::TransformComponent* transform = state.scene->transforms.GetComponent(instance.entity);
        const float delta = transform == nullptr ? 1.0e6f :
            std::abs(probe::coordinates::from_wicked(transform->translation_local).x - expected_x);
        const int32_t matches = transform != nullptr && delta <= 1.0e-5f ? 1 : 0;
        return matches;
    }
    return 0;
}

// A row's Wicked world matrix must be the change of basis S * M * S of the
// Elisa transform it was staged with, computed here from the matrix instead
// of the per-component rules the render service applies.
extern "C" int32_t elisa_render_scene_v1_test_snapshot_world_matches(int64_t render_id,
    float px, float py, float pz, float qx, float qy, float qz, float qw,
    float sx, float sy, float sz) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state) || state.scene == nullptr) return 0;
    // DirectXMath's row-vector matrix, stored row by row, is the column-major
    // layout of Elisa's column-vector matrix.
    XMFLOAT4X4 authored;
    XMStoreFloat4x4(&authored, XMMatrixScaling(sx, sy, sz) *
        XMMatrixRotationQuaternion(XMQuaternionNormalize(XMVectorSet(qx, qy, qz, qw))) *
        XMMatrixTranslation(px, py, pz));
    ElisaMatrixPayload elisa{};
    std::copy(&authored.m[0][0], &authored.m[0][0] + 16, elisa.values_column_major);
    ElisaMatrixPayload expected{};
    if (!elisa_matrix_to_wicked(&elisa, &expected)) return 0;
    for (const InstanceSlot& instance : state.instances) {
        if (!instance.live || instance.render_id != render_id) continue;
        const wi::scene::TransformComponent* transform = state.scene->transforms.GetComponent(instance.entity);
        if (transform == nullptr) return 0;
        const float* actual = &transform->world.m[0][0];
        for (size_t index = 0; index < 16; ++index) {
            if (std::abs(actual[index] - expected.values_column_major[index]) > 1.0e-4f) return 0;
        }
        return 1;
    }
    return 0;
}

extern "C" int32_t elisa_render_scene_v1_test_camera_perspective_matches(
    float width, float height, float fov, float near_clip, float far_clip) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state) || state.camera == nullptr) return 0;
    const wi::scene::CameraComponent& camera = *state.camera;
    const auto close = [](float left, float right) { return std::abs(left - right) <= 1.0e-5f; };
    return (camera._flags & wi::scene::CameraComponent::ORTHO) == 0 &&
        close(camera.width, width) && close(camera.height, height) &&
        close(camera.fov, fov) && close(camera.zNearP, near_clip) &&
        close(camera.zFarP, far_clip) ? 1 : 0;
}

extern "C" int32_t elisa_render_scene_v1_test_camera_orthographic_matches(
    float width, float height, float vertical_size) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state) || state.camera == nullptr) return 0;
    const wi::scene::CameraComponent& camera = *state.camera;
    const auto close = [](float left, float right) { return std::abs(left - right) <= 1.0e-5f; };
    return (camera._flags & wi::scene::CameraComponent::ORTHO) != 0 &&
        close(camera.width, width) && close(camera.height, height) &&
        close(camera.ortho_vertical_size, vertical_size) ? 1 : 0;
}

// A right-handed camera looking along f with up u shows f × u on the frame's
// right. Wicked's camera shows GetRight() = Up × At there. Read back into Elisa
// space, Wicked's eye, view direction and screen right must match the look-at.
extern "C" int32_t elisa_render_scene_v1_test_camera_view_matches(
    float eye_x, float eye_y, float eye_z, float target_x, float target_y, float target_z,
    float up_x, float up_y, float up_z) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state) || state.camera == nullptr) return 0;
    const XMVECTOR forward = XMVector3Normalize(XMVectorSet(target_x - eye_x,
        target_y - eye_y, target_z - eye_z, 0.0f));
    XMFLOAT3 expected_forward;
    XMFLOAT3 expected_right;
    XMStoreFloat3(&expected_forward, forward);
    XMStoreFloat3(&expected_right, XMVector3Normalize(XMVector3Cross(forward,
        XMVectorSet(up_x, up_y, up_z, 0.0f))));
    XMFLOAT3 wicked_right;
    XMStoreFloat3(&wicked_right, XMVector3Normalize(state.camera->GetRight()));
    const XMFLOAT3 eye = probe::coordinates::from_wicked(state.camera->Eye);
    const XMFLOAT3 at = probe::coordinates::from_wicked_direction(state.camera->At);
    const XMFLOAT3 right = probe::coordinates::from_wicked_direction(wicked_right);
    const auto close = [](const XMFLOAT3& left, const XMFLOAT3& right) {
        return std::abs(left.x - right.x) <= 1.0e-4f && std::abs(left.y - right.y) <= 1.0e-4f &&
            std::abs(left.z - right.z) <= 1.0e-4f;
    };
    return close(eye, XMFLOAT3(eye_x, eye_y, eye_z)) && close(at, expected_forward) &&
        close(right, expected_right) ? 1 : 0;
}

extern "C" int32_t elisa_render_scene_v1_test_camera_render_target_matches(
    int32_t width, int32_t height, int32_t expected_live) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state) || state.camera == nullptr) return 0;
    const auto& target = state.camera->render_to_texture;
    const bool live = target.resolution.x == uint32_t(width) &&
        target.resolution.y == uint32_t(height) &&
        (target.rendertarget_render.IsValid() || target.rendertarget_display.IsValid());
    return live == (expected_live != 0) ? 1 : 0;
}

extern "C" int32_t elisa_render_scene_v1_test_instances_share_mesh(int64_t first, int64_t second) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state) || state.scene == nullptr) return 0;
    size_t first_slot = MAX_INSTANCES;
    size_t second_slot = MAX_INSTANCES;
    if (!valid_handle(state, first, first_slot) || !valid_handle(state, second, second_slot)) return 0;
    const wi::scene::ObjectComponent* first_object = state.scene->objects.GetComponent(state.instances[first_slot].entity);
    const wi::scene::ObjectComponent* second_object = state.scene->objects.GetComponent(state.instances[second_slot].entity);
    return first_object != nullptr && second_object != nullptr && first_object->meshID == second_object->meshID ? 1 : 0;
}

// The crossfade weight the next pose applies, after easing; -1 without a fade.
extern "C" float elisa_render_scene_v1_test_animation_blend_weight(int64_t handle) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state)) return -1.0f;
    size_t slot = MAX_INSTANCES;
    if (!valid_handle(state, handle, slot)) return -1.0f;
    const InstanceSlot& instance = state.instances[slot];
    if (instance.previous_animation_clip < 0 || instance.blend_duration <= 0.0f) return -1.0f;
    float blend = std::clamp(instance.blend_elapsed / instance.blend_duration, 0.0f, 1.0f);
    if (instance.blend_eased) blend = blend * blend * (3.0f - 2.0f * blend);
    return blend;
}

extern "C" uint64_t elisa_render_scene_v1_test_mesh_count(void) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state) || state.scene == nullptr) return 0;
    return state.scene->meshes.GetCount();
}

extern "C" uint64_t elisa_render_scene_v1_test_object_count(void) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state) || state.scene == nullptr) return 0;
    return state.scene->objects.GetCount();
}

// Test-only GPU readback. render_scene_native_smoke.py enables this symbol;
// ordinary game binaries do not include a blocking probe in the render ABI.
extern "C" int32_t elisa_render_scene_v1_last_frame_center_differs_from_corner(void) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state) || state.path == nullptr) return 0;
    wait_for_object_pipelines();
    if (wi::graphics::GetDevice() != nullptr) wi::graphics::GetDevice()->WaitForGPU();
    const wi::graphics::Texture& frame = state.path->GetRenderResult3D();
    const wi::graphics::TextureDesc& desc = frame.GetDesc();
    const size_t pixel_size = wi::graphics::GetFormatStride(desc.format);
    wi::vector<uint8_t> pixels;
    if (!frame.IsValid() || desc.width == 0 || desc.height == 0 || pixel_size == 0 ||
        !wi::helper::saveTextureToMemoryFile(frame, "RAW", pixels) ||
        pixels.size() < size_t(desc.width) * desc.height * pixel_size) {
        return 0;
    }
    const size_t center = (size_t(desc.height / 2) * desc.width + desc.width / 2) * pixel_size;
    bool differs = false;
    for (size_t index = 0; index < pixel_size; ++index) {
        differs = differs || pixels[index] != pixels[center + index];
    }
    return differs ? 1 : 0;
}

extern "C" int32_t elisa_render_scene_v1_last_frame_has_overlay_text(void) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state) || state.path == nullptr) return 0;
    if (wi::graphics::GetDevice() != nullptr) wi::graphics::GetDevice()->WaitForGPU();
    const wi::graphics::Texture& frame = state.path->GetRenderResult2D();
    const wi::graphics::TextureDesc& desc = frame.GetDesc();
    wi::vector<uint8_t> pixels;
    if (!frame.IsValid() || desc.format != wi::graphics::Format::R8G8B8A8_UNORM ||
        desc.width == 0 || desc.height == 0 ||
        !wi::helper::saveTextureToMemoryFile(frame, "RAW", pixels) ||
        pixels.size() < size_t(desc.width) * desc.height * RGBA8_CHANNEL_COUNT) return 0;
    const uint32_t max_x = std::min(desc.width, OVERLAY_TEXT_PROBE_WIDTH);
    const uint32_t max_y = std::min(desc.height, OVERLAY_TEXT_PROBE_HEIGHT);
    for (uint32_t y = 0; y < max_y; ++y) {
        for (uint32_t x = 0; x < max_x; ++x) {
            const size_t index = (size_t(y) * desc.width + x) * RGBA8_CHANNEL_COUNT;
            bool visible = false;
            for (size_t channel = 0; channel < RGBA8_COLOR_CHANNEL_COUNT; ++channel) {
                visible = visible || pixels[index + channel] > OVERLAY_TEXT_BRIGHTNESS_THRESHOLD;
            }
            if (pixels[index + RGBA8_ALPHA_INDEX] > OVERLAY_TEXT_ALPHA_THRESHOLD && visible) return 1;
        }
    }
    return 0;
}

extern "C" int32_t elisa_render_scene_v1_last_frame_has_overlay_panel(void) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state) || state.path == nullptr) return 0;
    if (wi::graphics::GetDevice() != nullptr) wi::graphics::GetDevice()->WaitForGPU();
    const wi::graphics::Texture& frame = state.path->GetRenderResult2D();
    const wi::graphics::TextureDesc& desc = frame.GetDesc();
    const uint32_t sample_x = state.path->LogicalToPhysical(OVERLAY_PANEL_PROBE_LOGICAL_X);
    const uint32_t sample_y = state.path->LogicalToPhysical(OVERLAY_PANEL_PROBE_LOGICAL_Y);
    wi::vector<uint8_t> pixels;
    if (!frame.IsValid() || desc.format != wi::graphics::Format::R8G8B8A8_UNORM ||
        desc.width <= sample_x || desc.height <= sample_y ||
        !wi::helper::saveTextureToMemoryFile(frame, "RAW", pixels) ||
        pixels.size() < size_t(desc.width) * desc.height * RGBA8_CHANNEL_COUNT) return 0;
    const size_t index =
        (size_t(sample_y) * desc.width + sample_x) * RGBA8_CHANNEL_COUNT;
    return pixels[index + RGBA8_ALPHA_INDEX] > OVERLAY_PANEL_ALPHA_THRESHOLD &&
        pixels[index] > pixels[index + 1] + OVERLAY_PANEL_COLOR_SEPARATION &&
        pixels[index] > pixels[index + 2] + OVERLAY_PANEL_COLOR_SEPARATION ? 1 : 0;
}

extern "C" uint64_t elisa_render_scene_v1_last_frame_center_patch_hash(void) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state) || state.path == nullptr) return 0;
    if (wi::graphics::GetDevice() != nullptr) wi::graphics::GetDevice()->WaitForGPU();
    const wi::graphics::Texture& frame = state.path->GetRenderResult3D();
    const wi::graphics::TextureDesc& desc = frame.GetDesc();
    const size_t pixel_size = wi::graphics::GetFormatStride(desc.format);
    wi::vector<uint8_t> pixels;
    if (!frame.IsValid() || desc.width == 0 || desc.height == 0 || pixel_size == 0 ||
        !wi::helper::saveTextureToMemory(frame, pixels) ||
        pixels.size() < size_t(desc.width) * desc.height * pixel_size) return 0;

    const uint32_t center_x = desc.width / 2;
    const uint32_t center_y = desc.height / 2;
    const uint32_t min_x = center_x > ARC_RENDER_PROBE_RADIUS ? center_x - ARC_RENDER_PROBE_RADIUS : 0;
    const uint32_t min_y = center_y > ARC_RENDER_PROBE_RADIUS ? center_y - ARC_RENDER_PROBE_RADIUS : 0;
    const uint32_t max_x = std::min(desc.width, center_x + ARC_RENDER_PROBE_RADIUS + 1);
    const uint32_t max_y = std::min(desc.height, center_y + ARC_RENDER_PROBE_RADIUS + 1);
    uint64_t hash = FNV_OFFSET_BASIS;
    for (uint32_t y = min_y; y < max_y; ++y) {
        for (uint32_t x = min_x; x < max_x; ++x) {
            const size_t offset = (size_t(y) * desc.width + x) * pixel_size;
            for (size_t channel = 0; channel < pixel_size; ++channel) {
                hash ^= pixels[offset + channel];
                hash *= FNV_PRIME;
            }
        }
    }
    return hash;
}

extern "C" int32_t elisa_render_scene_v1_test_arc_capacity(void) {
    std::array<int64_t, MAX_ELECTRIC_ARCS> handles{};
    size_t created = 0;
    for (; created < MAX_ELECTRIC_ARCS; ++created) {
        const int64_t handle = elisa_render_scene_v1_create_electric_arc(0.02f, 0.0f, 7);
        if (handle <= 0) break;
        handles[created] = handle;
    }

    const int64_t overflow = created == MAX_ELECTRIC_ARCS
        ? elisa_render_scene_v1_create_electric_arc(0.02f, 0.0f, 7)
        : ELISA_RENDER_SCENE_BACKEND_FAILED;
    bool cleanup_ok = true;
    if (overflow > 0) cleanup_ok = elisa_render_scene_v1_destroy_electric_arc(overflow) == ELISA_RENDER_SCENE_OK;
    for (size_t index = 0; index < created; ++index) {
        cleanup_ok = (elisa_render_scene_v1_destroy_electric_arc(handles[index]) == ELISA_RENDER_SCENE_OK) && cleanup_ok;
    }
    return created == MAX_ELECTRIC_ARCS && overflow == ELISA_RENDER_SCENE_CAPACITY && cleanup_ok ? 1 : 0;
}

#include "render_scene_subset_probe.h"
