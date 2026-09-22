#pragma once

// Parser for bounded camera and KHR_lights_punctual records in a cooked mesh.

namespace elisa::assets::detail {

inline bool scene_floats(const probe::PackageIndex& package, const std::string& key,
    size_t count, std::vector<float>& values) {
    return decode_floats(package, key.c_str(), count, values);
}

inline bool parse_geometry_scene(const probe::PackageIndex& package, CookedGeometry& geometry,
    std::string& error) {
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
