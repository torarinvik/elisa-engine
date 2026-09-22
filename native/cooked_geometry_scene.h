#pragma once

// Parser for bounded camera and KHR_lights_punctual records in a cooked mesh.

namespace elisa::assets::detail {

inline bool scene_floats(const probe::PackageIndex& package, const std::string& key,
    size_t count, std::vector<float>& values) {
    return decode_floats(package, key.c_str(), count, values);
}

inline bool parse_geometry_scene(const probe::PackageIndex& package, CookedGeometry& geometry,
    std::string& error) {
    const auto mesh_section = package.sections.find("mesh_count");
    const bool has_mesh_metadata = mesh_section != package.sections.end() ||
        package.sections.count("mesh_placement_count") != 0 ||
        package.sections.count("mesh_placement_stride") != 0 ||
        package.sections.count("mesh_placements_b64") != 0;
    if (has_mesh_metadata) {
        uint64_t mesh_count = 0;
        uint64_t placement_count = 0;
        const auto stride = package.sections.find("mesh_placement_stride");
        if (!parse_count(package, "mesh_count", mesh_count) || mesh_count == 0 || mesh_count > 256 ||
            !parse_count(package, "mesh_placement_count", placement_count) ||
            placement_count < mesh_count || placement_count > 256 || stride == package.sections.end() ||
            stride->second != "56") {
            error = "invalid cooked geometry mesh placement metadata";
            return false;
        }
        const auto encoded = package.sections.find("mesh_placements_b64");
        std::vector<uint8_t> bytes;
        if (encoded == package.sections.end() || !decode_base64(encoded->second, bytes) ||
            bytes.size() != size_t(placement_count) * 56u) {
            error = "invalid cooked geometry mesh placement records";
            return false;
        }
        auto read_u32 = [&bytes](size_t offset) -> uint32_t {
            return uint32_t(bytes[offset]) | (uint32_t(bytes[offset + 1]) << 8) |
                (uint32_t(bytes[offset + 2]) << 16) | (uint32_t(bytes[offset + 3]) << 24);
        };
        geometry.mesh_count = uint32_t(mesh_count);
        for (size_t index = 0; index < size_t(placement_count); ++index) {
            const size_t offset = index * 56u;
            CookedGeometry::MeshPlacement placement;
            placement.mesh = read_u32(offset);
            placement.node = read_u32(offset + 4);
            if (placement.mesh >= geometry.mesh_count || placement.node >= 256) {
                error = "cooked geometry mesh placement index is out of range";
                return false;
            }
            for (size_t component = 0; component < placement.transform.size(); ++component) {
                const uint32_t bits = read_u32(offset + 8 + component * 4);
                std::memcpy(&placement.transform[component], &bits, sizeof(bits));
                if (!std::isfinite(placement.transform[component])) {
                    error = "cooked geometry mesh placement transform is not finite";
                    return false;
                }
            }
            geometry.mesh_placements.push_back(placement);
        }
        std::vector<bool> seen(geometry.mesh_count, false);
        for (const auto& placement : geometry.mesh_placements) seen[placement.mesh] = true;
        if (std::find(seen.begin(), seen.end(), false) != seen.end()) {
            error = "cooked geometry mesh placement leaves a mesh unplaced";
            return false;
        }
    }
    const auto camera_section = package.sections.find("camera_count");
    if (camera_section != package.sections.end()) {
        uint64_t count = 0;
        if (!parse_count(package, "camera_count", count) || count > 16) {
            error = "invalid cooked geometry camera count";
            return false;
        }
        for (size_t index = 0; index < size_t(count); ++index) {
            const std::string prefix = "camera_" + std::to_string(index) + "_";
            uint64_t node = 0;
            uint64_t projection = 0;
            float x = 0.0f, y = 0.0f, near_clip = 0.0f, far_clip = 0.0f;
            std::vector<float> transform;
            if (!parse_count(package, (prefix + "node").c_str(), node) || node >= 256 ||
                !parse_count(package, (prefix + "projection").c_str(), projection) || projection > 1 ||
                !parse_finite_float(package, prefix + "x", x) || x <= 0.0f ||
                !parse_finite_float(package, prefix + "y", y) || y <= 0.0f ||
                !parse_finite_float(package, prefix + "near", near_clip) || near_clip <= 0.0f ||
                !parse_finite_float(package, prefix + "far", far_clip) || far_clip <= near_clip ||
                !scene_floats(package, prefix + "transform_b64", 12, transform)) {
                error = "invalid cooked geometry camera metadata";
                return false;
            }
            CookedGeometry::Camera camera;
            camera.node = uint32_t(node);
            camera.projection = uint32_t(projection);
            camera.x = x;
            camera.y = y;
            camera.near_clip = near_clip;
            camera.far_clip = far_clip;
            std::copy_n(transform.data(), 12, camera.transform.data());
            geometry.cameras.push_back(camera);
        }
    }
    const auto light_section = package.sections.find("light_count");
    if (light_section != package.sections.end()) {
        uint64_t count = 0;
        if (!parse_count(package, "light_count", count) || count > 32) {
            error = "invalid cooked geometry light count";
            return false;
        }
        for (size_t index = 0; index < size_t(count); ++index) {
            const std::string prefix = "light_" + std::to_string(index) + "_";
            uint64_t node = 0;
            uint64_t kind = 0;
            float intensity = 0.0f, range = 0.0f, inner = 0.0f, outer = 0.0f;
            std::vector<float> color;
            std::vector<float> transform;
            if (!parse_count(package, (prefix + "node").c_str(), node) || node >= 256 ||
                !parse_count(package, (prefix + "kind").c_str(), kind) || kind > 2 ||
                !parse_finite_float(package, prefix + "intensity", intensity) || intensity < 0.0f ||
                !parse_finite_float(package, prefix + "range", range) || range < 0.0f ||
                !parse_finite_float(package, prefix + "inner", inner) || inner < 0.0f ||
                !parse_finite_float(package, prefix + "outer", outer) || outer < inner ||
                !scene_floats(package, prefix + "color_b64", 3, color) ||
                !scene_floats(package, prefix + "transform_b64", 12, transform)) {
                error = "invalid cooked geometry light metadata";
                return false;
            }
            if (std::any_of(color.begin(), color.end(), [](float channel) { return channel < 0.0f; })) {
                error = "invalid cooked geometry light color";
                return false;
            }
            CookedGeometry::Light light;
            light.node = uint32_t(node);
            light.kind = uint32_t(kind);
            light.intensity = intensity;
            light.range = range;
            light.inner_cone = inner;
            light.outer_cone = outer;
            std::copy_n(color.data(), 3, light.color.data());
            std::copy_n(transform.data(), 12, light.transform.data());
            geometry.lights.push_back(light);
        }
    }
    return true;
}

} // namespace elisa::assets::detail
