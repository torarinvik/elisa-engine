#pragma once

// Private Wicked texture assignment for the engine-owned render-scene ABI.
#include "cooked_geometry_package.h"
#include "wiResourceManager.h"
#include "wiScene.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>

namespace elisa::rendering::textures {

enum class Slot : int32_t {
    BaseColor = 0,
    Normal = 1,
    Surface = 2,
    Emissive = 3,
    Count = 4,
};

inline constexpr int32_t SLOT_COUNT = static_cast<int32_t>(Slot::Count);
inline constexpr std::array<uint32_t, SLOT_COUNT> MATERIAL_SLOTS = {
    wi::scene::MaterialComponent::BASECOLORMAP,
    wi::scene::MaterialComponent::NORMALMAP,
    wi::scene::MaterialComponent::SURFACEMAP,
    wi::scene::MaterialComponent::EMISSIVEMAP,
};

inline bool valid_slot(int32_t slot) {
    return slot >= 0 && slot < SLOT_COUNT;
}

inline bool assign_resolved_texture(wi::scene::MaterialComponent& material,
        int32_t slot_code, const std::filesystem::path& resolved_path) {
    if (!valid_slot(slot_code)) return false;
    if (resolved_path.empty()) return false;

    auto flags = wi::resourcemanager::Flags::IMPORT_BLOCK_COMPRESSED;
    if (slot_code == static_cast<int32_t>(Slot::Normal)) {
        flags |= wi::resourcemanager::Flags::IMPORT_NORMALMAP;
    }
    wi::Resource resource = wi::resourcemanager::Load(resolved_path.string(), flags);
    if (!resource.IsValid() || !resource.GetTexture().IsValid()) return false;

    std::string resource_name = resolved_path.string();
    auto& texture = material.textures[MATERIAL_SLOTS[static_cast<size_t>(slot_code)]];
    texture.resource = std::move(resource);
    texture.name.swap(resource_name);
    material.SetDirty();
    return true;
}

inline bool assign_project_texture(wi::scene::MaterialComponent& material,
        int32_t slot_code, const char* asset_path) {
    std::filesystem::path resolved_path;
    if (!elisa::assets::resolve_project_asset_path(asset_path, resolved_path)) return false;
    return assign_resolved_texture(material, slot_code, resolved_path);
}

} // namespace elisa::rendering::textures
