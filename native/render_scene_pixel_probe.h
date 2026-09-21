#pragma once

#include <algorithm>

namespace {

constexpr uint32_t ARC_RENDER_PROBE_RADIUS = 8;
constexpr uint32_t OVERLAY_TEXT_PROBE_WIDTH = 240;
constexpr uint32_t OVERLAY_TEXT_PROBE_HEIGHT = 80;
constexpr uint8_t OVERLAY_TEXT_ALPHA_THRESHOLD = 16;
constexpr uint8_t OVERLAY_TEXT_BRIGHTNESS_THRESHOLD = 80;
constexpr size_t RGBA8_COLOR_CHANNEL_COUNT = 3;
constexpr size_t RGBA8_CHANNEL_COUNT = 4;
constexpr size_t RGBA8_ALPHA_INDEX = 3;
constexpr uint64_t FNV_OFFSET_BASIS = 14695981039346656037ull;
constexpr uint64_t FNV_PRIME = 1099511628211ull;

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
        return transform != nullptr && std::abs(transform->translation_local.x - expected_x) <= 1.0e-5f ? 1 : 0;
    }
    return 0;
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
    for (uint32_t attempt = 0; attempt < PIPELINE_WAIT_ATTEMPTS; ++attempt) {
        if (wi::renderer::IsPipelineCreationActive() == 0) break;
        wi::helper::Sleep(PIPELINE_WAIT_MILLISECONDS);
    }
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
