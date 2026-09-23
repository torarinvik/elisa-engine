#pragma once

// Parser for the optional dense morph streams emitted by the glTF cooker.

namespace elisa::assets::detail {

inline bool parse_geometry_morphs(const probe::PackageIndex& package, CookedGeometry& geometry,
    uint64_t vertex_count, std::string& error) {
    const auto count_section = package.sections.find("morph_targets");
    const auto default_stride = package.sections.find("morph_default_weights_stride");
    const auto default_data = package.sections.find("morph_default_weights_b64");
    const bool has_defaults = default_stride != package.sections.end() || default_data != package.sections.end();
    size_t fields = 0;
    for (const char* key : {"morph_targets", "morph_target_position_stride"})
        fields += package.sections.count(key);
    if (count_section == package.sections.end()) {
        if (fields != 0 || has_defaults) {
            error = "incomplete cooked geometry morph metadata";
            return false;
        }
        return true;
    }
    uint64_t count = 0;
    if (fields != 2 || !parse_count(package, "morph_targets", count) || count == 0 || count > 32 ||
        package.sections.at("morph_target_position_stride") != "12") {
        error = "invalid cooked geometry morph metadata";
        return false;
    }
    const auto normal_stride = package.sections.find("morph_target_normal_stride");
    const bool has_normals = normal_stride != package.sections.end();
    if (has_normals && normal_stride->second != "12") {
        error = "invalid cooked geometry morph normal stride";
        return false;
    }
    const size_t placement_count = geometry.mesh_placements.empty() ? 1 : geometry.mesh_placements.size();
    geometry.morph_default_weights.assign(placement_count * size_t(count), 0.0f);
    if (has_defaults && (default_stride == package.sections.end() || default_stride->second != "4" ||
        default_data == package.sections.end() || !decode_floats(package, "morph_default_weights_b64",
            geometry.morph_default_weights.size(), geometry.morph_default_weights))) {
        error = "invalid cooked geometry morph default weights";
        return false;
    }
    for (size_t index = 0; index < size_t(count); ++index) {
        const std::string prefix = "morph_" + std::to_string(index) + "_";
        const auto positions = package.sections.find(prefix + "positions_b64");
        const auto normals = package.sections.find(prefix + "normals_b64");
        if (positions == package.sections.end() || (has_normals != (normals != package.sections.end()))) {
            error = "invalid cooked geometry morph position stream";
            return false;
        }
        CookedGeometry::MorphTarget target;
        if (!decode_floats(package, (prefix + "positions_b64").c_str(), size_t(vertex_count) * 3,
                target.positions)) {
            error = "invalid cooked geometry morph position stream";
            return false;
        }
        if (has_normals && !decode_floats(package, (prefix + "normals_b64").c_str(), size_t(vertex_count) * 3,
                target.normals)) {
            error = "invalid cooked geometry morph normal stream";
            return false;
        }
        geometry.morph_targets.push_back(std::move(target));
    }
    return true;
}

} // namespace elisa::assets::detail
