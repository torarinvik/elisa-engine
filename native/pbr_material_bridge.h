#pragma once

// Validated PBR material updates behind generation-checked Wicked handles.
#include "probe_core.h"
#include "wiScene.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <string>

namespace probe {

enum class NativeAlphaMode : uint8_t { Opaque, Mask, Blend };

struct NativePbrDesc {
    XMFLOAT4 base_color = XMFLOAT4(1, 1, 1, 1);
    XMFLOAT4 emissive = XMFLOAT4(0, 0, 0, 0);
    float metallic = 0.0f;
    float roughness = 0.5f;
    float alpha_cutoff = 0.5f;
    NativeAlphaMode alpha_mode = NativeAlphaMode::Opaque;
    bool double_sided = false;
    bool cast_shadow = true;
};

struct NativeMaterialHandle {
    uint32_t slot = UINT32_MAX;
    uint32_t generation = 0;
    uintptr_t owner = 0;
};

class PbrMaterialBridge {
public:
    static constexpr uint32_t MAX_MATERIALS = 32;

    explicit PbrMaterialBridge(wi::scene::Scene& scene)
        : scene_(scene), owner_(reinterpret_cast<uintptr_t>(this)) {}
    PbrMaterialBridge(const PbrMaterialBridge&) = delete;
    PbrMaterialBridge& operator=(const PbrMaterialBridge&) = delete;

    NativeMaterialHandle create(const NativePbrDesc& desc) {
        if (!valid(desc)) return {};
        const uint32_t slot = free_slot();
        if (slot == MAX_MATERIALS) return {};
        const auto entity = scene_.Entity_CreateCube("elisa_material_" + std::to_string(slot));
        auto& state = slots_[slot];
        if (entity == wi::ecs::INVALID_ENTITY) return {};
        state.generation = state.generation == UINT32_MAX ? 0 : state.generation + 1;
        if (state.generation == 0) {
            scene_.Entity_Remove(entity);
            return {};
        }
        state.entity = entity;
        state.live = true;
        apply(entity, desc);
        return {slot, state.generation, owner_};
    }

    bool update(NativeMaterialHandle handle, const NativePbrDesc& desc) {
        if (!live(handle) || !valid(desc)) return false;
        apply(slots_[handle.slot].entity, desc);
        return true;
    }

    bool set_texture(NativeMaterialHandle handle, uint32_t slot, const wi::Resource& resource) {
        if (!live(handle) || slot >= wi::scene::MaterialComponent::TEXTURESLOT_COUNT || !resource.IsValid()) return false;
        auto* material = scene_.materials.GetComponent(slots_[handle.slot].entity);
        if (material == nullptr) return false;
        material->textures[slot].resource = resource;
        material->SetDirty();
        return true;
    }

    bool destroy(NativeMaterialHandle handle) {
        if (!live(handle)) return false;
        scene_.Entity_Remove(slots_[handle.slot].entity);
        slots_[handle.slot].entity = wi::ecs::INVALID_ENTITY;
        slots_[handle.slot].live = false;
        return true;
    }

    bool live(NativeMaterialHandle handle) const {
        return handle.owner == owner_ && handle.slot < MAX_MATERIALS && slots_[handle.slot].live &&
            slots_[handle.slot].generation == handle.generation;
    }

private:
    struct Slot { wi::ecs::Entity entity = wi::ecs::INVALID_ENTITY; uint32_t generation = 0; bool live = false; };

    static bool finite4(const XMFLOAT4& value) {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) &&
            std::isfinite(value.w);
    }

    static bool unit4(const XMFLOAT4& value) {
        return value.x >= 0.0f && value.x <= 1.0f && value.y >= 0.0f && value.y <= 1.0f &&
            value.z >= 0.0f && value.z <= 1.0f && value.w >= 0.0f && value.w <= 1.0f;
    }

    static bool valid(const NativePbrDesc& desc) {
        return finite4(desc.base_color) && finite4(desc.emissive) && unit4(desc.base_color) &&
            desc.emissive.x >= 0.0f && desc.emissive.y >= 0.0f && desc.emissive.z >= 0.0f &&
            desc.emissive.w >= 0.0f && std::isfinite(desc.metallic) && desc.metallic >= 0.0f &&
            desc.metallic <= 1.0f && std::isfinite(desc.roughness) && desc.roughness >= 0.04f &&
            desc.roughness <= 1.0f && std::isfinite(desc.alpha_cutoff) &&
            desc.alpha_cutoff >= 0.0f && desc.alpha_cutoff <= 1.0f &&
            desc.alpha_mode <= NativeAlphaMode::Blend;
    }

    void apply(wi::ecs::Entity entity, const NativePbrDesc& desc) {
        auto* material = scene_.materials.GetComponent(entity);
        if (material == nullptr) return;
        material->SetBaseColor(desc.base_color);
        material->SetEmissiveColor(desc.emissive);
        material->SetMetalness(desc.metallic);
        material->SetRoughness(desc.roughness);
        material->SetAlphaRef(desc.alpha_mode == NativeAlphaMode::Mask ? desc.alpha_cutoff : 1.0f);
        material->userBlendMode = desc.alpha_mode == NativeAlphaMode::Blend
            ? wi::enums::BLENDMODE_ALPHA : wi::enums::BLENDMODE_OPAQUE;
        material->SetCastShadow(desc.cast_shadow);
        if (desc.double_sided) material->_flags |= wi::scene::MaterialComponent::DOUBLE_SIDED;
        else material->_flags &= ~wi::scene::MaterialComponent::DOUBLE_SIDED;
        material->SetDirty();
    }

    uint32_t free_slot() const { for (uint32_t i = 0; i < MAX_MATERIALS; ++i) if (!slots_[i].live) return i; return MAX_MATERIALS; }

    wi::scene::Scene& scene_;
    uintptr_t owner_;
    std::array<Slot, MAX_MATERIALS> slots_{};
};

inline bool probe_pbr_material_bridge(wi::scene::Scene& scene) {
    const size_t before = scene.objects.GetCount();
    PbrMaterialBridge bridge(scene);
    NativePbrDesc desc;
    desc.base_color = XMFLOAT4(0.2f, 0.6f, 0.9f, 1.0f);
    desc.emissive = XMFLOAT4(0.01f, 0.02f, 0.04f, 2.0f);
    desc.metallic = 0.8f;
    desc.roughness = 0.25f;
    desc.alpha_mode = NativeAlphaMode::Mask;
    desc.alpha_cutoff = 0.42f;
    desc.double_sided = true;
    const auto handle = bridge.create(desc);
    wi::graphics::TextureDesc texture_desc;
    texture_desc.width = texture_desc.height = texture_desc.depth = texture_desc.array_size = 1;
    texture_desc.mip_levels = texture_desc.sample_count = 1;
    texture_desc.format = wi::graphics::Format::R8G8B8A8_UNORM;
    texture_desc.bind_flags = wi::graphics::BindFlag::SHADER_RESOURCE;
    const uint32_t pixel = 0xFF3366CCu;
    wi::graphics::SubresourceData texture_data{&pixel, 4, 4};
    wi::graphics::Texture texture;
    const bool texture_created = wi::graphics::GetDevice()->CreateTexture(&texture_desc, &texture_data, &texture);
    wi::Resource texture_resource;
    if (texture_created) texture_resource.SetTexture(texture);
    auto* material = scene.materials.GetComponent(scene.Entity_FindByName("elisa_material_0"));
    if (!check(bridge.live(handle), "pbr material creates a handle") ||
        !check(material != nullptr && material->IsAlphaTestEnabled() && material->alphaRef == 0.42f,
            "pbr material applies alpha mask") ||
        !check(bridge.update(handle, desc) && !bridge.update(
            NativeMaterialHandle{handle.slot, handle.generation, 0}, desc),
            "pbr material updates and rejects foreign handle") ||
        !check(texture_created && bridge.set_texture(handle, wi::scene::MaterialComponent::BASECOLORMAP,
            texture_resource) && !bridge.set_texture(handle, wi::scene::MaterialComponent::TEXTURESLOT_COUNT,
            texture_resource), "pbr material binds a texture slot") ||
        !check(!([&]() { NativePbrDesc invalid = desc; invalid.roughness = 0.0f; return bridge.create(invalid).owner; })(),
            "pbr material rejects invalid roughness")) return false;
    desc.alpha_mode = NativeAlphaMode::Blend;
    if (!check(bridge.update(handle, desc) && material->GetBlendMode() == wi::enums::BLENDMODE_ALPHA,
        "pbr material applies alpha blend")) return false;
    desc.alpha_mode = NativeAlphaMode::Opaque;
    if (!check(bridge.update(handle, desc) && material->GetBlendMode() == wi::enums::BLENDMODE_OPAQUE &&
            material->alphaRef == 1.0f, "pbr material applies opaque alpha")) return false;
    if (!check(bridge.destroy(handle) && !bridge.live(handle) && scene.objects.GetCount() == before,
        "pbr material unloads without object growth")) return false;
    return true;
}

} // namespace probe
