#pragma once

// Test-only hooks for multi-material snapshot meshes, built into the render
// smoke host with ELISA_RENDER_SCENE_TEST_PROBE.
#include <cstdio>
#include <cstring>
#include <cmath>
#include <filesystem>

namespace {

constexpr float DOMINANT_CHANNEL_RATIO = 2.0f;
constexpr float DOMINANT_CHANNEL_MINIMUM = 0.02f;
constexpr uint32_t DOMINANT_CHANNEL_RADIUS = 2;
constexpr int32_t DOMINANT_CHANNEL_NONE = -1;
constexpr int32_t DOMINANT_CHANNEL_UNREADABLE = -2;

const InstanceSlot* snapshot_instance(const RenderSceneService& state, int64_t render_id) {
    if (!state.initialized || !on_owner_thread(state) || state.scene == nullptr) return nullptr;
    for (const InstanceSlot& instance : state.instances) {
        if (instance.live && instance.render_id == render_id) return &instance;
    }
    return nullptr;
}

const wi::scene::MeshComponent* snapshot_instance_mesh(const RenderSceneService& state, int64_t render_id,
    const wi::scene::ObjectComponent** object_out = nullptr, size_t placement_index = 0) {
    const InstanceSlot* instance = snapshot_instance(state, render_id);
    if (instance == nullptr) return nullptr;
    wi::ecs::Entity object_entity = instance->entity;
    if (placement_index != 0) {
        if (placement_index - 1 >= instance->snapshot_placement_entities.size()) return nullptr;
        object_entity = instance->snapshot_placement_entities[placement_index - 1];
    }
    const wi::scene::ObjectComponent* object = state.scene->objects.GetComponent(object_entity);
    if (object == nullptr) return nullptr;
    if (object_out != nullptr) *object_out = object;
    return state.scene->meshes.GetComponent(object->meshID);
}

// An unsigned float with `mantissa_bits` of mantissa and a 5-bit exponent,
// as packed in R11G11B10_FLOAT.
float decode_small_float(uint32_t bits, uint32_t mantissa_bits) {
    const uint32_t mantissa = bits & ((1u << mantissa_bits) - 1u);
    const uint32_t exponent = bits >> mantissa_bits;
    const float fraction = float(mantissa) / float(1u << mantissa_bits);
    if (exponent == 0) return std::ldexp(fraction, -14);
    if (exponent == 31) return std::numeric_limits<float>::infinity();
    return std::ldexp(1.0f + fraction, int(exponent) - 15);
}

bool decode_frame_pixel(wi::graphics::Format format, const uint8_t* pixel, float rgb[3]) {
    if (format == wi::graphics::Format::R11G11B10_FLOAT) {
        uint32_t packed = 0;
        std::memcpy(&packed, pixel, sizeof(packed));
        rgb[0] = decode_small_float(packed & 0x7FFu, 6);
        rgb[1] = decode_small_float((packed >> 11) & 0x7FFu, 6);
        rgb[2] = decode_small_float(packed >> 22, 5);
        return true;
    }
    if (format == wi::graphics::Format::R8G8B8A8_UNORM) {
        for (size_t channel = 0; channel < 3; ++channel) rgb[channel] = pixel[channel] / 255.0f;
        return true;
    }
    return false;
}

} // namespace

// The render smoke has run out of distinct exit codes, so newer test groups
// exit with their group code and log the failing case here.
extern "C" int32_t elisa_render_scene_v1_test_failed_case(int32_t group, int32_t case_number) {
    std::fprintf(stderr, "render scene test group %d failed at case %d\n", group, case_number);
    std::fflush(stderr);
    return group;
}

extern "C" int32_t elisa_render_scene_v1_test_snapshot_subset_count(int64_t render_id) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    const wi::scene::MeshComponent* mesh = snapshot_instance_mesh(state, render_id);
    return mesh == nullptr ? -1 : int32_t(mesh->subsets.size());
}

// 1 when the instance's mesh subset covers exactly these indices and draws
// with the registered material `material_high:material_low`.
extern "C" int32_t elisa_render_scene_v1_test_snapshot_subset_matches(int64_t render_id, uint32_t subset,
    uint32_t index_offset, uint32_t index_count, uint64_t material_high, uint64_t material_low) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    const wi::scene::MeshComponent* mesh = snapshot_instance_mesh(state, render_id);
    const size_t material = snapshot_material_asset_slot(state, material_high, material_low);
    if (mesh == nullptr || subset >= mesh->subsets.size() || material == MAX_SNAPSHOT_MATERIAL_ASSETS) return 0;
    const wi::scene::MeshComponent::MeshSubset& actual = mesh->subsets[subset];
    return actual.indexOffset == index_offset && actual.indexCount == index_count &&
        actual.materialID == state.snapshot_material_assets[material].material_entity ? 1 : 0;
}

extern "C" int32_t elisa_render_scene_v1_test_snapshot_placement_count(int64_t render_id) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    const InstanceSlot* instance = snapshot_instance(state, render_id);
    return instance == nullptr || instance->gameplay_epoch <= 0
        ? -1 : int32_t(instance->snapshot_placement_entities.size() + 1);
}

extern "C" int32_t elisa_render_scene_v1_test_snapshot_placement_subset_matches(int64_t render_id,
    uint32_t placement_index, uint32_t subset, uint32_t index_offset, uint32_t index_count,
    uint64_t material_high, uint64_t material_low) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    const wi::scene::MeshComponent* mesh = snapshot_instance_mesh(state, render_id, nullptr, placement_index);
    const size_t material = snapshot_material_asset_slot(state, material_high, material_low);
    if (mesh == nullptr || subset >= mesh->subsets.size() || material == MAX_SNAPSHOT_MATERIAL_ASSETS) return 0;
    const wi::scene::MeshComponent::MeshSubset& actual = mesh->subsets[subset];
    return actual.indexOffset == index_offset && actual.indexCount == index_count &&
        actual.materialID == state.snapshot_material_assets[material].material_entity ? 1 : 0;
}

extern "C" int32_t elisa_render_scene_v1_test_snapshot_casts_shadow(int64_t render_id) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    const wi::scene::ObjectComponent* object = nullptr;
    if (snapshot_instance_mesh(state, render_id, &object) == nullptr || object == nullptr) return -1;
    return object->IsCastingShadow() ? 1 : 0;
}

extern "C" int32_t elisa_render_scene_v1_test_instance_casts_shadow(int64_t render_id) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state)) return -1;
    size_t slot = MAX_INSTANCES;
    if (!valid_handle(state, render_id, slot)) return -1;
    const wi::scene::ObjectComponent* object = state.scene->objects.GetComponent(state.instances[slot].entity);
    return object == nullptr ? -1 : (object->IsCastingShadow() ? 1 : 0);
}

extern "C" uint64_t elisa_render_scene_v1_test_snapshot_material_set_count(void) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state)) return UINT64_MAX;
    uint64_t count = 0;
    for (const SnapshotMaterialSet& set : state.snapshot_material_sets.sets) count += set.live ? 1 : 0;
    return count;
}

// 1 when registered material `high:low` holds exactly these factors, alpha
// mode, and sidedness, both as registered and in the Wicked material drawn
// with it: blending, shadow casting, and alpha reference follow the mode.
extern "C" int32_t elisa_render_scene_v1_test_snapshot_material_matches(uint64_t high, uint64_t low,
    float red, float green, float blue, float alpha, float metallic, float roughness,
    float emissive_red, float emissive_green, float emissive_blue, float alpha_cutoff,
    int32_t alpha_mode, int32_t double_sided) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state) || state.scene == nullptr) return 0;
    const size_t slot = snapshot_material_asset_slot(state, high, low);
    if (slot == MAX_SNAPSHOT_MATERIAL_ASSETS) return 0;
    const SnapshotMaterialAssetSlot& asset = state.snapshot_material_assets[slot];
    const auto close = [](float actual, float expected) {
        return std::isfinite(actual) && std::abs(actual - expected) <= 1.0e-5f;
    };
    bool authored = close(asset.base_color[0], red) && close(asset.base_color[1], green) &&
        close(asset.base_color[2], blue) && close(asset.base_color[3], alpha) &&
        close(asset.metallic, metallic) && close(asset.roughness, roughness) &&
        close(asset.emissive[0], emissive_red) && close(asset.emissive[1], emissive_green) &&
        close(asset.emissive[2], emissive_blue) && close(asset.alpha_cutoff, alpha_cutoff) &&
        asset.alpha_mode == alpha_mode && asset.double_sided == (double_sided != 0);
    const wi::scene::MaterialComponent* material = state.scene->materials.GetComponent(asset.material_entity);
    if (!authored || material == nullptr) return 0;
    const bool blended = alpha_mode == ELISA_RENDER_SCENE_ALPHA_BLEND;
    const float alpha_ref = alpha_mode == ELISA_RENDER_SCENE_ALPHA_MASK ? alpha_cutoff : OPAQUE_SNAPSHOT_ALPHA_REF;
    const bool drawn = close(material->baseColor.x, red) && close(material->baseColor.y, green) &&
        close(material->baseColor.z, blue) && close(material->baseColor.w, alpha) &&
        close(material->metalness, metallic) && close(material->roughness, roughness) &&
        close(material->emissiveColor.x, emissive_red) && close(material->emissiveColor.y, emissive_green) &&
        close(material->emissiveColor.z, emissive_blue) && close(material->alphaRef, alpha_ref) &&
        material->IsDoubleSided() == (double_sided != 0) && material->IsCastingShadow() == !blended &&
        material->userBlendMode == (blended ? wi::enums::BLENDMODE_ALPHA : wi::enums::BLENDMODE_OPAQUE);
    return drawn ? 1 : 0;
}

// The color channel (0 red, 1 green, 2 blue) that dominates a 5x5 patch of
// the last 3D frame around (x, y) in thousandths of the frame size: its mean
// exceeds twice each other channel's. -1 when none dominates, -2 when the
// frame can't be read. It first waits for pending object pipelines, so a frame
// drawn before they were ready is only stale until the next pump.
extern "C" int32_t elisa_render_scene_v1_test_last_frame_dominant_channel(uint32_t x_permille, uint32_t y_permille) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state) || state.path == nullptr ||
        x_permille > 1000 || y_permille > 1000) return DOMINANT_CHANNEL_UNREADABLE;
    wait_for_object_pipelines();
    if (wi::graphics::GetDevice() != nullptr) wi::graphics::GetDevice()->WaitForGPU();
    const wi::graphics::Texture& frame = state.path->GetRenderResult3D();
    const wi::graphics::TextureDesc& desc = frame.GetDesc();
    const size_t pixel_size = wi::graphics::GetFormatStride(desc.format);
    wi::vector<uint8_t> pixels;
    if (!frame.IsValid() || desc.width <= 2 * DOMINANT_CHANNEL_RADIUS || desc.height <= 2 * DOMINANT_CHANNEL_RADIUS ||
        pixel_size == 0 || !wi::helper::saveTextureToMemory(frame, pixels) ||
        pixels.size() < size_t(desc.width) * desc.height * pixel_size) return DOMINANT_CHANNEL_UNREADABLE;
    const uint32_t center_x = std::clamp(uint32_t(uint64_t(desc.width - 1) * x_permille / 1000),
        DOMINANT_CHANNEL_RADIUS, desc.width - 1 - DOMINANT_CHANNEL_RADIUS);
    const uint32_t center_y = std::clamp(uint32_t(uint64_t(desc.height - 1) * y_permille / 1000),
        DOMINANT_CHANNEL_RADIUS, desc.height - 1 - DOMINANT_CHANNEL_RADIUS);
    float sum[3] = {};
    for (uint32_t y = center_y - DOMINANT_CHANNEL_RADIUS; y <= center_y + DOMINANT_CHANNEL_RADIUS; ++y) {
        for (uint32_t x = center_x - DOMINANT_CHANNEL_RADIUS; x <= center_x + DOMINANT_CHANNEL_RADIUS; ++x) {
            float rgb[3] = {};
            if (!decode_frame_pixel(desc.format, pixels.data() + (size_t(y) * desc.width + x) * pixel_size, rgb)) {
                return DOMINANT_CHANNEL_UNREADABLE;
            }
            for (size_t channel = 0; channel < 3; ++channel) sum[channel] += rgb[channel];
        }
    }
    for (int32_t channel = 0; channel < 3; ++channel) {
        const float value = sum[channel];
        const float first_other = sum[(channel + 1) % 3];
        const float second_other = sum[(channel + 2) % 3];
        const float patch = float((2 * DOMINANT_CHANNEL_RADIUS + 1) * (2 * DOMINANT_CHANNEL_RADIUS + 1));
        if (std::isfinite(value) && value > DOMINANT_CHANNEL_MINIMUM * patch &&
            value > DOMINANT_CHANNEL_RATIO * first_other && value > DOMINANT_CHANNEL_RATIO * second_other) {
            return channel;
        }
    }
    return DOMINANT_CHANNEL_NONE;
}

// 1 when registered material `high:low` names texture
// `texture_high:texture_low` in `slot_code`, a zero ID for none, and the
// Wicked material drawn with it has a texture in that slot exactly when it
// names one.
extern "C" int32_t elisa_render_scene_v1_test_snapshot_material_texture_id(uint64_t high, uint64_t low,
    int32_t slot_code, uint64_t texture_high, uint64_t texture_low) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state) || state.scene == nullptr ||
        !elisa::rendering::textures::valid_slot(slot_code)) return 0;
    const size_t slot = snapshot_material_asset_slot(state, high, low);
    if (slot == MAX_SNAPSHOT_MATERIAL_ASSETS) return 0;
    const SnapshotMaterialAssetSlot& asset = state.snapshot_material_assets[slot];
    const wi::scene::MaterialComponent* material = state.scene->materials.GetComponent(asset.material_entity);
    if (material == nullptr) return 0;
    const size_t index = static_cast<size_t>(slot_code);
    const wi::Resource& resource = material->textures[elisa::rendering::textures::MATERIAL_SLOTS[index]].resource;
    const bool drawn = resource.IsValid() && resource.GetTexture().IsValid();
    const bool named = texture_high != 0 || texture_low != 0;
    return asset.texture_high[index] == texture_high && asset.texture_low[index] == texture_low &&
        drawn == named ? 1 : 0;
}

// 1 when registered material `high:low` reads occlusion from its surface
// texture, 0 when it doesn't, and -1 when it is missing or its Wicked
// material disagrees.
extern "C" int32_t elisa_render_scene_v1_test_snapshot_material_occlusion(uint64_t high, uint64_t low) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state) || state.scene == nullptr) return -1;
    const size_t slot = snapshot_material_asset_slot(state, high, low);
    if (slot == MAX_SNAPSHOT_MATERIAL_ASSETS) return -1;
    const SnapshotMaterialAssetSlot& asset = state.snapshot_material_assets[slot];
    const wi::scene::MaterialComponent* material = state.scene->materials.GetComponent(asset.material_entity);
    if (material == nullptr || material->IsOcclusionEnabled_Primary() != asset.occlusion) return -1;
    return asset.occlusion ? 1 : 0;
}

// Overwrites cooked file `destination` with a copy of `source`, so a test can
// rewrite a bundle after a mesh loaded from it. Both are existing project
// paths under build/cooked/. 1 on success.
extern "C" int32_t elisa_render_scene_v1_test_replace_cooked_file(const char* source, const char* destination) {
    std::filesystem::path from;
    std::filesystem::path to;
    const auto cooked = [](const char* path) { return std::strncmp(path, "build/cooked/", 13) == 0; };
    if (!elisa::assets::resolve_project_asset_path(source, from) ||
        !elisa::assets::resolve_project_asset_path(destination, to) || !cooked(source) || !cooked(destination)) {
        return 0;
    }
    std::error_code error;
    std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing, error);
    return error ? 0 : 1;
}
