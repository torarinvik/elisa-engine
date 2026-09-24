#pragma once

#include "fbx_asset_import_skin.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace elisa::assets::detail {

inline constexpr size_t MAX_FBX_MATERIAL_SLOTS = 16;

inline bool fbx_unit_scalar(const ufbx_material_map& map, float fallback, float& value) {
    const double source = map.has_value ? double(map.value_real) : double(fallback);
    if (!std::isfinite(source) || source < 0.0 || source > 1.0) return false;
    value = float(source);
    return std::isfinite(value);
}

inline bool fbx_unit_color(const ufbx_material_map& map, const ufbx_material_map& fallback_map,
    const std::array<float, 4>& fallback, std::array<float, 4>& value) {
    const ufbx_material_map& source = map.has_value ? map : fallback_map;
    value = fallback;
    if (!source.has_value) return true;
    if (source.value_components < 3 || source.value_components > 4) return false;
    const double channels[4] = {double(source.value_vec4.x), double(source.value_vec4.y),
        double(source.value_vec4.z), source.value_components == 4 ? double(source.value_vec4.w) : 1.0};
    for (size_t channel = 0; channel < 4; ++channel) {
        if (!std::isfinite(channels[channel]) || channels[channel] < 0.0 || channels[channel] > 1.0) return false;
        value[channel] = float(channels[channel]);
    }
    return true;
}

inline bool fbx_map_has_texture(const ufbx_material_map& map) {
    return map.texture_enabled && map.texture != nullptr;
}

inline bool fbx_texture_source_path(const ufbx_material_map& map, std::string& path,
    FbxImportResult& result) {
    path.clear();
    if (!fbx_map_has_texture(map)) return true;
    const ufbx_texture& texture = *map.texture;
    if (texture.type != UFBX_TEXTURE_FILE || texture.content.size != 0 || texture.video != nullptr ||
        texture.has_uv_transform || texture.wrap_u != UFBX_WRAP_REPEAT ||
        texture.wrap_v != UFBX_WRAP_REPEAT || texture.uv_set.length != 0) {
        fail(result, "FBX textures must be direct external files with default UVs and repeat wrapping");
        return false;
    }
    const ufbx_string filename = texture.relative_filename.length != 0
        ? texture.relative_filename : texture.filename;
    if (filename.data == nullptr || filename.length == 0 || filename.length > 4096) {
        fail(result, "FBX texture has no bounded relative file path");
        return false;
    }
    path.assign(filename.data, filename.length);
    if (path.front() == '/' || path.find_first_of("\\:\r\n") != std::string::npos ||
        path.find('\0') != std::string::npos) {
        fail(result, "FBX texture paths must be safe relative paths");
        return false;
    }
    size_t start = 0;
    while (start < path.size()) {
        const size_t end = path.find('/', start);
        const std::string component = path.substr(start,
            end == std::string::npos ? std::string::npos : end - start);
        if (component.empty() || component == "." || component == "..") {
            fail(result, "FBX texture paths cannot contain empty, current, or parent directories");
            return false;
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return true;
}

inline bool fbx_extract_texture_role(const ufbx_material_map& preferred,
    const ufbx_material_map& fallback, std::string& path, FbxImportResult& result) {
    const ufbx_material_map* selected = nullptr;
    if (fbx_map_has_texture(preferred)) selected = &preferred;
    if (fbx_map_has_texture(fallback)) {
        if (selected != nullptr && selected->texture != fallback.texture) {
            fail(result, "FBX material has conflicting textures for one supported material role");
            return false;
        }
        selected = &fallback;
    }
    if (selected == nullptr) {
        path.clear();
        return true;
    }
    return fbx_texture_source_path(*selected, path, result);
}

inline bool extract_fbx_material(const ufbx_material& source, FbxMaterialData& output,
    FbxImportResult& result, bool ignore_textures = false) {
    const ufbx_material_map* supported[] = {
        &source.pbr.base_color, &source.fbx.diffuse_color,
        &source.pbr.normal_map, &source.fbx.normal_map,
        &source.pbr.emission_color, &source.fbx.emission_color,
        &source.pbr.ambient_occlusion, &source.pbr.roughness, &source.pbr.metalness,
    };
    if (!ignore_textures) {
        for (size_t index = 0; index < UFBX_MATERIAL_PBR_MAP_COUNT; ++index) {
            const ufbx_material_map& map = source.pbr.maps[index];
            if (!fbx_map_has_texture(map)) continue;
            bool allowed = false;
            for (const ufbx_material_map* candidate : supported) allowed = allowed || &map == candidate;
            if (!allowed) {
                fail(result, "FBX material uses a texture role that the runtime cannot represent yet");
                return false;
            }
        }
        for (size_t index = 0; index < UFBX_MATERIAL_FBX_MAP_COUNT; ++index) {
            const ufbx_material_map& map = source.fbx.maps[index];
            if (!fbx_map_has_texture(map)) continue;
            bool allowed = false;
            for (const ufbx_material_map* candidate : supported) allowed = allowed || &map == candidate;
            if (!allowed) {
                fail(result, "FBX material uses a texture role that the runtime cannot represent yet");
                return false;
            }
        }
    }
    if (source.name.length > 256 || (source.name.length != 0 && source.name.data == nullptr)) {
        fail(result, "FBX material name exceeds the 256-byte limit");
        return false;
    }
    output.name.assign(source.name.data ? source.name.data : "", source.name.length);
    if (output.name.find('\0') != std::string::npos) {
        fail(result, "FBX material name contains a null byte");
        return false;
    }

    constexpr std::array<float, 4> WHITE{1.0f, 1.0f, 1.0f, 1.0f};
    const ufbx_material_map& base_factor = source.pbr.base_factor.has_value
        ? source.pbr.base_factor : source.fbx.diffuse_factor;
    float base_multiplier = 1.0f;
    if (!fbx_unit_scalar(base_factor, 1.0f, base_multiplier) ||
        !fbx_unit_color(source.pbr.base_color, source.fbx.diffuse_color, WHITE, output.base_color)) {
        fail(result, "FBX material base-color factors must be finite and in [0, 1]");
        return false;
    }
    for (size_t channel = 0; channel < 3; ++channel) output.base_color[channel] *= base_multiplier;

    if (!fbx_unit_scalar(source.pbr.metalness, 0.0f, output.metallic) ||
        !fbx_unit_scalar(source.pbr.roughness, 1.0f, output.roughness)) {
        fail(result, "FBX metallic and roughness factors must be finite and in [0, 1]");
        return false;
    }
    constexpr std::array<float, 4> BLACK{0.0f, 0.0f, 0.0f, 1.0f};
    const ufbx_material_map& emission_factor = source.pbr.emission_factor.has_value
        ? source.pbr.emission_factor : source.fbx.emission_factor;
    const ufbx_material_map& emission_color = source.pbr.emission_color.has_value
        ? source.pbr.emission_color : source.fbx.emission_color;
    float emission_multiplier = 1.0f;
    std::array<float, 4> emission{};
    if (!fbx_unit_scalar(emission_factor, 1.0f, emission_multiplier) ||
        !fbx_unit_color(emission_color, source.fbx.emission_color, BLACK, emission)) {
        fail(result, "FBX emissive factors must be finite and in [0, 1]");
        return false;
    }
    for (size_t channel = 0; channel < 3; ++channel) {
        output.emissive[channel] = emission[channel] * emission_multiplier;
    }

    float transparency = 0.0f;
    if (!fbx_unit_scalar(source.fbx.transparency_factor, 0.0f, transparency)) {
        fail(result, "FBX transparency factor must be finite and in [0, 1]");
        return false;
    }
    output.base_color[3] *= 1.0f - transparency;
    output.alpha_mode = output.base_color[3] < 1.0f ? 2u : 0u;
    output.double_sided = source.features.double_sided.enabled;
    if (ignore_textures) return true;
    if (!fbx_extract_texture_role(source.pbr.base_color, source.fbx.diffuse_color,
            output.texture_sources[0], result) ||
        !fbx_extract_texture_role(source.pbr.normal_map, source.fbx.normal_map,
            output.texture_sources[1], result) ||
        !fbx_extract_texture_role(source.pbr.emission_color, source.fbx.emission_color,
            output.texture_sources[3], result) ||
        !fbx_texture_source_path(source.pbr.ambient_occlusion,
            output.texture_sources[4], result) ||
        !fbx_texture_source_path(source.pbr.roughness,
            output.surface_texture_sources[0], result) ||
        !fbx_texture_source_path(source.pbr.metalness,
            output.surface_texture_sources[1], result)) return false;
    output.occlusion = !output.texture_sources[4].empty();
    return true;
}


} // namespace elisa::assets::detail

#include "fbx_asset_import_geometry.h"
