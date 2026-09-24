#pragma once

// Material records cooked for a mesh's material slots: glTF factors, alpha
// policy, and the bundle image sections each slot samples.
#include "bundle_texture.h"
#include "cooked_package_fields.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace elisa::assets {

inline constexpr uint32_t MAX_GEOMETRY_TEXTURES = 64;
inline constexpr size_t SLOT_MATERIAL_TEXTURES = 8;
inline constexpr size_t SLOT_MATERIAL_SURFACE = 2;
inline constexpr size_t SLOT_MATERIAL_OCCLUSION_TEXTURE = 4;
inline constexpr size_t SLOT_MATERIAL_CLEARCOAT = 5;
inline constexpr size_t SLOT_MATERIAL_CLEARCOAT_ROUGHNESS = 6;
inline constexpr size_t SLOT_MATERIAL_CLEARCOAT_NORMAL = 7;
inline constexpr uint32_t SLOT_MATERIAL_DOUBLE_SIDED = 1;
inline constexpr uint32_t SLOT_MATERIAL_OCCLUSION = 2;
inline constexpr uint32_t SLOT_ALPHA_MASK = 1;
inline constexpr float WICKED_NORMAL_SCALE_LIMIT = 65504.0f;
inline constexpr float MIN_COOKED_OCCLUSION_STRENGTH = 0.0f;
inline constexpr float MAX_COOKED_OCCLUSION_STRENGTH = 1.0f;

// glTF factors authored for one material slot. Alpha mode uses the render
// scene codes: 0 opaque, 1 mask, 2 blend.
struct CookedSlotMaterial {
    std::array<float, 4> base_color{1.0f, 1.0f, 1.0f, 1.0f};
    float metallic = 1.0f;
    float roughness = 1.0f;
    std::array<float, 3> emissive{};
    float alpha_cutoff = 0.5f;
    float normal_scale = 1.0f;
    float occlusion_strength = 1.0f;
    float clearcoat_factor = 0.0f;
    float clearcoat_roughness = 0.0f;
    float clearcoat_normal_scale = 1.0f;
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

inline bool valid_utf8(const std::string& value) {
    size_t index = 0;
    while (index < value.size()) {
        const uint8_t first = uint8_t(value[index]);
        if (first <= 0x7f) {
            ++index;
            continue;
        }
        const size_t count = first >= 0xc2 && first <= 0xdf ? 2 :
            first >= 0xe0 && first <= 0xef ? 3 : first >= 0xf0 && first <= 0xf4 ? 4 : 0;
        if (count == 0 || count > value.size() - index) return false;
        uint32_t codepoint = first & (count == 2 ? 0x1f : count == 3 ? 0x0f : 0x07);
        for (size_t byte = 1; byte < count; ++byte) {
            const uint8_t continuation = uint8_t(value[index + byte]);
            if ((continuation & 0xc0) != 0x80) return false;
            codepoint = (codepoint << 6) | (continuation & 0x3f);
        }
        if ((count == 3 && codepoint < 0x800) || (count == 4 && codepoint < 0x10000) ||
            (codepoint >= 0xd800 && codepoint <= 0xdfff) || codepoint > 0x10ffff) return false;
        index += count;
    }
    return true;
}

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

// Normal scale is an additive sidecar so older 48-byte material records and
// FBX packages keep their existing layout and default to the glTF value 1.
inline bool parse_slot_normal_scales(const probe::PackageIndex& package,
    std::vector<CookedSlotMaterial>& materials, std::string& error) {
    const size_t present = package.sections.count("slot_normal_scale_stride") +
        package.sections.count("slot_normal_scales_b64");
    if (present == 0) return true;
    std::vector<uint32_t> words;
    if (present != 2 || materials.empty() || package.sections.at("slot_normal_scale_stride") != "4" ||
        !decode_u32(package, "slot_normal_scales_b64", materials.size(), words)) {
        error = "invalid cooked slot normal scales";
        return false;
    }
    for (size_t slot = 0; slot < materials.size(); ++slot) {
        float scale = 0.0f;
        std::memcpy(&scale, &words[slot], sizeof(scale));
        if (!std::isfinite(scale) || std::abs(scale) > WICKED_NORMAL_SCALE_LIMIT) {
            error = "cooked slot normal scale is out of range";
            return false;
        }
        materials[slot].normal_scale = scale;
    }
    return true;
}

inline bool parse_slot_occlusion_strengths(const probe::PackageIndex& package,
    std::vector<CookedSlotMaterial>& materials, std::string& error) {
    const size_t present = package.sections.count("slot_occlusion_strength_stride") +
        package.sections.count("slot_occlusion_strengths_b64");
    if (present == 0) return true;
    std::vector<uint32_t> words;
    if (present != 2 || materials.empty() ||
        package.sections.at("slot_occlusion_strength_stride") != "4" ||
        !decode_u32(package, "slot_occlusion_strengths_b64", materials.size(), words)) {
        error = "invalid cooked slot occlusion strengths";
        return false;
    }
    for (size_t slot = 0; slot < materials.size(); ++slot) {
        float strength = 0.0f;
        std::memcpy(&strength, &words[slot], sizeof(strength));
        if (!std::isfinite(strength) || strength < MIN_COOKED_OCCLUSION_STRENGTH ||
            strength > MAX_COOKED_OCCLUSION_STRENGTH) {
            error = "cooked slot occlusion strength is out of range";
            return false;
        }
        materials[slot].occlusion_strength = strength;
    }
    return true;
}

// Clearcoat values are an additive sidecar so existing 48-byte material
// records and legacy five-texture records keep their original defaults.
inline bool parse_slot_clearcoat_factors(const probe::PackageIndex& package,
    std::vector<CookedSlotMaterial>& materials, std::string& error) {
    const size_t present = package.sections.count("slot_clearcoat_factor_stride") +
        package.sections.count("slot_clearcoat_factors_b64");
    std::vector<uint32_t> words;
    if (present == 0) return true;
    if (present != 2 || materials.empty() ||
        package.sections.at("slot_clearcoat_factor_stride") != "12" ||
        !decode_u32(package, "slot_clearcoat_factors_b64", materials.size() * 3, words)) {
        error = "invalid cooked slot clearcoat factors";
        return false;
    }
    for (size_t slot = 0; slot < materials.size(); ++slot) {
        float factors[3]{};
        std::memcpy(factors, words.data() + slot * 3, sizeof(factors));
        if (!std::isfinite(factors[0]) || factors[0] < 0.0f || factors[0] > 1.0f ||
            !std::isfinite(factors[1]) || factors[1] < 0.0f || factors[1] > 1.0f ||
            !std::isfinite(factors[2]) || std::abs(factors[2]) > WICKED_NORMAL_SCALE_LIMIT) {
            error = "cooked slot clearcoat factors are out of range";
            return false;
        }
        materials[slot].clearcoat_factor = factors[0];
        materials[slot].clearcoat_roughness = factors[1];
        materials[slot].clearcoat_normal_scale = factors[2];
    }
    return true;
}

inline bool parse_slot_material_names(const probe::PackageIndex& package, uint32_t material_slots,
    const std::vector<CookedSlotMaterial>& materials, std::vector<std::string>& names,
    std::string& error) {
    const auto found = package.sections.find("slot_material_names_b64");
    if (found == package.sections.end()) return true;
    std::vector<uint8_t> bytes;
    if (materials.size() != material_slots ||
        !decode_base64(found->second, bytes) || !decode_names(bytes, material_slots, names)) {
        error = "invalid cooked slot material names";
        return false;
    }
    for (const std::string& name : names) {
        if (name.size() > 256 || name.find('\0') != std::string::npos || !valid_utf8(name)) {
            error = "invalid cooked slot material name";
            return false;
        }
    }
    return true;
}

inline bool texture_section_name_valid(const std::string& name) {
    return name.size() == std::strlen(name.c_str()) && valid_bundle_section_name(name.c_str()) &&
        name != "mesh" && name != "manifest";
}

// Slot texture records are all present or all absent and need slot material
// records. The names list the bundle image sections the slots sample, each
// at least once; legacy 20-byte records name five images and current 32-byte
// records also carry clearcoat, clearcoat-roughness, and clearcoat-normal
// images. Masking
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
        const std::string stride = package.sections.count("slot_texture_stride")
            ? package.sections.at("slot_texture_stride") : std::string{};
        const size_t references_per_slot = stride == "20" ? 5 : stride == "32" ? 8 : 0;
        if (present != 4 || materials.empty() || !parse_count(package, "texture_count", count) ||
            count == 0 || count > MAX_GEOMETRY_TEXTURES || references_per_slot == 0 ||
            !decode_base64(package.sections.at("texture_names_b64"), names) ||
            !decode_names(names, size_t(count), sections) ||
            !decode_u32(package, "slot_textures_b64", materials.size() * references_per_slot, words)) {
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
            for (size_t texture = 0; texture < references_per_slot; ++texture) {
                const uint32_t reference = words[slot * references_per_slot + texture];
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
