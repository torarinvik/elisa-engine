#pragma once

// Material records cooked for a mesh's material slots: glTF factors, alpha
// policy, and the bundle image sections each slot samples.
#include "bundle_texture.h"
#include "cooked_package_fields.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace elisa::assets {

inline constexpr uint32_t MAX_GEOMETRY_TEXTURES = 64;
inline constexpr size_t SLOT_MATERIAL_TEXTURES = 5;
inline constexpr size_t SLOT_MATERIAL_SURFACE = 2;
inline constexpr size_t SLOT_MATERIAL_OCCLUSION_TEXTURE = 4;
inline constexpr uint32_t SLOT_MATERIAL_DOUBLE_SIDED = 1;
inline constexpr uint32_t SLOT_MATERIAL_OCCLUSION = 2;
inline constexpr uint32_t SLOT_ALPHA_MASK = 1;

// glTF factors authored for one material slot. Alpha mode uses the render
// scene codes: 0 opaque, 1 mask, 2 blend.
struct CookedSlotMaterial {
    std::array<float, 4> base_color{1.0f, 1.0f, 1.0f, 1.0f};
    float metallic = 1.0f;
    float roughness = 1.0f;
    std::array<float, 3> emissive{};
    float alpha_cutoff = 0.5f;
    uint32_t alpha_mode = 0;
    bool double_sided = false;
    // The surface image's red channel is ambient occlusion.
    bool occlusion = false;
    // The base color, normal, surface (roughness, metalness), emissive and
    // occlusion images: 0 for none, otherwise one more than the image's index
    // in the mesh's texture sections.
    std::array<uint32_t, SLOT_MATERIAL_TEXTURES> textures{};
};

namespace detail {

// Slot material records are both present or both absent and need explicit
// subset records. Each 48-byte record holds ten factors in [0, 1] (base
// color, metallic, roughness, emissive, alpha cutoff), the alpha mode, and
// flags: bit 0 marks a double-sided material, bit 1 occlusion.
inline bool parse_slot_materials(const probe::PackageIndex& package, uint32_t material_slots,
    std::vector<CookedSlotMaterial>& materials, std::string& error) {
    const size_t present = package.sections.count("slot_material_stride") +
        package.sections.count("slot_materials_b64");
    if (present == 0) return true;
    std::vector<uint32_t> words;
    if (present != 2 || package.sections.count("material_slots") == 0 ||
        package.sections.at("slot_material_stride") != "48" ||
        !decode_u32(package, "slot_materials_b64", size_t(material_slots) * 12, words)) {
        error = "invalid cooked slot material records";
        return false;
    }
    for (uint32_t slot = 0; slot < material_slots; ++slot) {
        const uint32_t* record = words.data() + size_t(slot) * 12;
        std::array<float, 10> factors{};
        std::memcpy(factors.data(), record, sizeof(float) * factors.size());
        const bool factors_valid = std::all_of(factors.begin(), factors.end(),
            [](float factor) { return factor >= 0.0f && factor <= 1.0f; });
        if (!factors_valid || record[10] > 2 ||
            (record[11] & ~(SLOT_MATERIAL_DOUBLE_SIDED | SLOT_MATERIAL_OCCLUSION)) != 0) {
            error = "cooked slot material is out of range";
            return false;
        }
        CookedSlotMaterial material;
        std::copy(factors.begin(), factors.begin() + 4, material.base_color.begin());
        material.metallic = factors[4];
        material.roughness = factors[5];
        std::copy(factors.begin() + 6, factors.begin() + 9, material.emissive.begin());
        material.alpha_cutoff = factors[9];
        material.alpha_mode = record[10];
        material.double_sided = (record[11] & SLOT_MATERIAL_DOUBLE_SIDED) != 0;
        material.occlusion = (record[11] & SLOT_MATERIAL_OCCLUSION) != 0;
        materials.push_back(material);
    }
    return true;
}

inline bool texture_section_name_valid(const std::string& name) {
    return name.size() == std::strlen(name.c_str()) && valid_bundle_section_name(name.c_str()) &&
        name != "mesh" && name != "manifest";
}

// Slot texture records are all present or all absent and need slot material
// records. The names list the bundle image sections the slots sample, each
// at least once; each 20-byte slot record names its five images. Masking
// needs a base color image and occlusion needs either a surface or an
// occlusion image.
inline bool parse_slot_textures(const probe::PackageIndex& package, std::vector<CookedSlotMaterial>& materials,
    std::vector<std::string>& sections, std::string& error) {
    size_t present = 0;
    for (const char* key : {"texture_count", "texture_names_b64", "slot_texture_stride", "slot_textures_b64"}) {
        present += package.sections.count(key);
    }
    if (present != 0) {
        uint64_t count = 0;
        std::vector<uint8_t> names;
        std::vector<uint32_t> words;
        if (present != 4 || materials.empty() || !parse_count(package, "texture_count", count) ||
            count == 0 || count > MAX_GEOMETRY_TEXTURES || package.sections.at("slot_texture_stride") != "20" ||
            !decode_base64(package.sections.at("texture_names_b64"), names) ||
            !decode_names(names, size_t(count), sections) ||
            !decode_u32(package, "slot_textures_b64", materials.size() * SLOT_MATERIAL_TEXTURES, words)) {
            error = "invalid cooked slot texture records";
            return false;
        }
        for (size_t index = 0; index < sections.size(); ++index) {
            if (!texture_section_name_valid(sections[index]) ||
                std::find(sections.begin(), sections.begin() + index, sections[index]) != sections.begin() + index) {
                error = "invalid cooked texture section name";
                return false;
            }
        }
        std::vector<bool> sampled(sections.size(), false);
        for (size_t slot = 0; slot < materials.size(); ++slot) {
            for (size_t texture = 0; texture < SLOT_MATERIAL_TEXTURES; ++texture) {
                const uint32_t reference = words[slot * SLOT_MATERIAL_TEXTURES + texture];
                if (reference > sections.size()) {
                    error = "cooked slot texture names a missing image";
                    return false;
                }
                if (reference != 0) sampled[reference - 1] = true;
                materials[slot].textures[texture] = reference;
            }
        }
        if (std::find(sampled.begin(), sampled.end(), false) != sampled.end()) {
            error = "cooked texture image is never sampled";
            return false;
        }
    }
    for (const CookedSlotMaterial& material : materials) {
        if ((material.alpha_mode == SLOT_ALPHA_MASK && material.textures[0] == 0) ||
            (material.occlusion && material.textures[SLOT_MATERIAL_SURFACE] == 0 &&
                material.textures[SLOT_MATERIAL_OCCLUSION_TEXTURE] == 0)) {
            error = "cooked slot material lacks the texture it needs";
            return false;
        }
    }
    return true;
}

} // namespace detail

} // namespace elisa::assets
