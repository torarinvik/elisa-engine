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
#include <vector>

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

inline wi::resourcemanager::Flags import_flags(int32_t slot_code) {
    auto flags = wi::resourcemanager::Flags::IMPORT_BLOCK_COMPRESSED;
    if (slot_code == static_cast<int32_t>(Slot::Normal)) {
        flags |= wi::resourcemanager::Flags::IMPORT_NORMALMAP;
    }
    return flags;
}

inline bool assign_resource(wi::scene::MaterialComponent& material, int32_t slot_code,
        wi::Resource resource, std::string resource_name) {
    if (!resource.IsValid() || !resource.GetTexture().IsValid()) return false;
    auto& texture = material.textures[MATERIAL_SLOTS[static_cast<size_t>(slot_code)]];
    texture.resource = std::move(resource);
    texture.name.swap(resource_name);
    material.SetDirty();
    return true;
}

inline bool assign_resolved_texture(wi::scene::MaterialComponent& material,
        int32_t slot_code, const std::filesystem::path& resolved_path) {
    if (!valid_slot(slot_code)) return false;
    if (resolved_path.empty()) return false;
    wi::Resource resource = wi::resourcemanager::Load(resolved_path.string(), import_flags(slot_code));
    return assign_resource(material, slot_code, std::move(resource), resolved_path.string());
}

// Decodes an image the caller already read from a bundle. Wicked caches by
// name, so the name must change whenever the bytes or import flags change;
// Wicked also picks the decoder from the name's extension.
inline bool assign_encoded_texture(wi::scene::MaterialComponent& material, int32_t slot_code,
        const std::string& resource_stem, const char* extension, const std::vector<uint8_t>& encoded) {
    if (!valid_slot(slot_code) || resource_stem.empty() || extension == nullptr || encoded.empty()) {
        return false;
    }
    std::string resource_name = resource_stem;
    if (slot_code == static_cast<int32_t>(Slot::Normal)) resource_name += ".normal";
    resource_name += ".";
    resource_name += extension;
    wi::Resource resource = wi::resourcemanager::Load(resource_name, import_flags(slot_code),
        encoded.data(), encoded.size());
    return assign_resource(material, slot_code, std::move(resource), std::move(resource_name));
}

inline bool assign_project_texture(wi::scene::MaterialComponent& material,
        int32_t slot_code, const char* asset_path) {
    std::filesystem::path resolved_path;
    if (!elisa::assets::resolve_project_asset_path(asset_path, resolved_path)) return false;
    return assign_resolved_texture(material, slot_code, resolved_path);
}

} // namespace elisa::rendering::textures
