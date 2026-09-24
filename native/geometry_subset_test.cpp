// Checks the production cooked-geometry loader against a manifest of packages.
// Each tab-separated manifest line is either
//   accept <path> <index count> <material slots> (<start> <count> <slot>)...
//          [materials (<10 factors> <alpha mode> <flags> <5 image references>)...]
//          [normal_scales (<scale>)...]
//          [occlusion_strengths (<strength>)...]
//          [sections (<name> <checksum>)...]
//   reject <path> <expected error text>
// and the loader must accept with exactly those subsets, slot materials and
// image sections (none when a marker is absent), or reject with an error
// that contains the expected text.
#include "cooked_geometry_package.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr size_t MATERIAL_FIELDS = 17;

std::vector<std::string> split_tabs(const std::string& line) {
    std::vector<std::string> fields;
    std::stringstream stream(line);
    std::string field;
    while (std::getline(stream, field, '\t')) fields.push_back(field);
    return fields;
}

bool check_slot_materials(const std::vector<std::string>& fields, size_t first, size_t end,
    const elisa::assets::CookedGeometry& geometry) {
    if ((end - first) % MATERIAL_FIELDS != 0 ||
        geometry.slot_materials.size() != (end - first) / MATERIAL_FIELDS) return false;
    for (size_t slot = 0; slot < geometry.slot_materials.size(); ++slot) {
        const auto& actual = geometry.slot_materials[slot];
        const float factors[10] = {actual.base_color[0], actual.base_color[1], actual.base_color[2],
            actual.base_color[3], actual.metallic, actual.roughness, actual.emissive[0],
            actual.emissive[1], actual.emissive[2], actual.alpha_cutoff};
        const size_t offset = first + slot * MATERIAL_FIELDS;
        for (size_t factor = 0; factor < 10; ++factor) {
            if (factors[factor] != std::stof(fields[offset + factor])) return false;
        }
        const unsigned long flags = std::stoul(fields[offset + 11]);
        if (actual.alpha_mode != std::stoul(fields[offset + 10]) ||
            actual.double_sided != ((flags & 1) != 0) || actual.occlusion != ((flags & 2) != 0)) return false;
        for (size_t texture = 0; texture < actual.textures.size(); ++texture) {
            if (actual.textures[texture] != std::stoul(fields[offset + 12 + texture])) return false;
        }
    }
    return true;
}

bool check_slot_normal_scales(const std::vector<std::string>& fields, size_t first, size_t end,
    const elisa::assets::CookedGeometry& geometry) {
    if (end - first != geometry.slot_materials.size()) return false;
    for (size_t slot = 0; slot < geometry.slot_materials.size(); ++slot) {
        if (geometry.slot_materials[slot].normal_scale != std::stof(fields[first + slot])) return false;
    }
    return true;
}

bool check_slot_occlusion_strengths(const std::vector<std::string>& fields, size_t first, size_t end,
    const elisa::assets::CookedGeometry& geometry) {
    if (end - first != geometry.slot_materials.size()) return false;
    for (size_t slot = 0; slot < geometry.slot_materials.size(); ++slot) {
        if (geometry.slot_materials[slot].occlusion_strength != std::stof(fields[first + slot])) return false;
    }
    return true;
}

bool check_sections(const std::vector<std::string>& fields, size_t first, size_t end,
    const elisa::assets::CookedGeometry& geometry) {
    if ((end - first) % 2 != 0 || geometry.texture_sections.size() != (end - first) / 2 ||
        geometry.texture_checksums.size() != geometry.texture_sections.size()) return false;
    for (size_t section = 0; section < geometry.texture_sections.size(); ++section) {
        if (geometry.texture_sections[section] != fields[first + section * 2] ||
            geometry.texture_checksums[section] != std::stoul(fields[first + section * 2 + 1])) return false;
    }
    return true;
}

bool check_slot_names(const std::vector<std::string>& fields, size_t first, size_t end,
    const elisa::assets::CookedGeometry& geometry) {
    if (geometry.slot_material_names.size() != end - first) return false;
    static constexpr char HEX[] = "0123456789abcdef";
    for (size_t slot = 0; slot < geometry.slot_material_names.size(); ++slot) {
        std::string encoded;
        for (unsigned char byte : geometry.slot_material_names[slot]) {
            encoded.push_back(HEX[byte >> 4]);
            encoded.push_back(HEX[byte & 15]);
        }
        if (encoded != fields[first + slot]) return false;
    }
    return true;
}

bool check_accepted(const std::vector<std::string>& fields, const elisa::assets::CookedGeometry& geometry) {
    if (fields.size() < 4) return false;
    const auto marker = [&fields](const char* name) {
        return size_t(std::find(fields.begin() + 4, fields.end(), name) - fields.begin());
    };
    const size_t materials = marker("materials");
    const size_t normal_scales = marker("normal_scales");
    const size_t occlusion_strengths = marker("occlusion_strengths");
    const size_t sections = marker("sections");
    const size_t slot_names = marker("slot_names");
    const size_t animations = marker("animations");
    const size_t morphs = marker("morphs");
    const size_t cameras = marker("cameras");
    const size_t lights = marker("lights");
    const size_t inverse_binds = marker("inverse_binds");
    const size_t uv1 = marker("uv1");
    const size_t mesh_placements = marker("mesh_placements");
    const size_t end = std::min({materials, sections, slot_names, animations, morphs, cameras, lights,
        inverse_binds, uv1, mesh_placements, normal_scales, occlusion_strengths});
    const size_t material_end = std::min({sections, slot_names, animations, morphs, cameras, lights,
        inverse_binds, uv1, mesh_placements, normal_scales, occlusion_strengths});
    const size_t section_end = std::min({slot_names, animations, morphs, cameras, lights,
        inverse_binds, uv1, mesh_placements, normal_scales, occlusion_strengths});
    if (materials == fields.size() ? !geometry.slot_materials.empty()
                                   : !check_slot_materials(fields, materials + 1, material_end, geometry)) return false;
    const size_t normal_scales_end = std::min({sections, slot_names, animations, morphs, cameras, lights,
        inverse_binds, uv1, mesh_placements, occlusion_strengths, fields.size()});
    if (normal_scales != fields.size()) {
        if (!check_slot_normal_scales(fields, normal_scales + 1, normal_scales_end, geometry)) return false;
    } else if (std::any_of(geometry.slot_materials.begin(), geometry.slot_materials.end(),
        [](const auto& material) { return material.normal_scale != 1.0f; })) return false;
    const size_t occlusion_strengths_end = std::min({sections, slot_names, animations, morphs, cameras, lights,
        inverse_binds, uv1, mesh_placements, fields.size()});
    if (occlusion_strengths != fields.size()) {
        if (!check_slot_occlusion_strengths(fields, occlusion_strengths + 1,
            occlusion_strengths_end, geometry)) return false;
    } else if (std::any_of(geometry.slot_materials.begin(), geometry.slot_materials.end(),
        [](const auto& material) { return material.occlusion_strength != 1.0f; })) return false;
    if (sections == fields.size() ? !geometry.texture_sections.empty() || !geometry.texture_checksums.empty()
                                  : !check_sections(fields, sections + 1, section_end, geometry)) return false;
    const size_t slot_names_end = std::min({animations, morphs, cameras, lights,
        inverse_binds, uv1, mesh_placements, normal_scales, occlusion_strengths});
    if (slot_names == fields.size() ? !geometry.slot_material_names.empty()
                                    : !check_slot_names(fields, slot_names + 1, slot_names_end, geometry)) return false;
    if (animations == fields.size() ? !geometry.animation_clips.empty()
                                    : geometry.animation_clips.size() != std::stoul(fields[animations + 1])) return false;
    if (morphs == fields.size() ? !geometry.morph_targets.empty()
                                : geometry.morph_targets.size() != std::stoul(fields[morphs + 1])) return false;
    if (cameras == fields.size() ? !geometry.cameras.empty()
                                 : geometry.cameras.size() != std::stoul(fields[cameras + 1])) return false;
    if (lights == fields.size() ? !geometry.lights.empty()
                                 : geometry.lights.size() != std::stoul(fields[lights + 1])) return false;
    if (inverse_binds == fields.size()) {
        if (!geometry.skin_inverse_bind_matrices.empty()) return false;
    } else {
        const size_t inverse_bind_end = std::min({uv1, mesh_placements, normal_scales,
            occlusion_strengths, fields.size()});
        if (inverse_bind_end <= inverse_binds ||
            geometry.skin_inverse_bind_matrices.size() != inverse_bind_end - inverse_binds - 1) return false;
        for (size_t component = 0; component < geometry.skin_inverse_bind_matrices.size(); ++component) {
            if (geometry.skin_inverse_bind_matrices[component] != std::stof(fields[inverse_binds + 1 + component])) {
                return false;
            }
        }
    }
    if (uv1 == fields.size()) {
        if (!geometry.uv1s.empty() || !geometry.uv1_source.empty()) return false;
    } else if (std::min({mesh_placements, normal_scales, occlusion_strengths, fields.size()}) - uv1 != 6 ||
        geometry.uv1s.size() != std::stoul(fields[uv1 + 1]) * 2 ||
        geometry.uv1_source != fields[uv1 + 2] ||
        geometry.uv1_generation_resolution != std::stoul(fields[uv1 + 3]) ||
        geometry.uv1_generation_padding != std::stoul(fields[uv1 + 4]) ||
        geometry.uv1_chart_count != std::stoul(fields[uv1 + 5])) return false;
    if (mesh_placements != fields.size()) {
        const size_t count = std::stoul(fields[mesh_placements + 1]);
        if (geometry.mesh_count != count || geometry.mesh_placements.size() != count ||
            fields.size() - mesh_placements != 2 + count * 20) return false;
        for (size_t index = 0; index < count; ++index) {
            const auto& actual = geometry.mesh_placements[index];
            const size_t offset = mesh_placements + 2 + index * 20;
            const uint32_t integers[8] = {actual.mesh, actual.node, actual.vertex_start,
                actual.vertex_count, actual.index_start, actual.index_count,
                actual.subset_start, actual.subset_count};
            for (size_t field = 0; field < 8; ++field) {
                if (integers[field] != std::stoul(fields[offset + field])) return false;
            }
            for (size_t component = 0; component < actual.transform.size(); ++component) {
                if (actual.transform[component] != std::stof(fields[offset + 8 + component])) return false;
            }
        }
    }
    if (end < 4 || (end - 4) % 3 != 0) return false;
    if (geometry.indices.size() != std::stoul(fields[2]) ||
        geometry.material_slots != std::stoul(fields[3]) ||
        geometry.subsets.size() != (end - 4) / 3) return false;
    for (size_t subset = 0; subset < geometry.subsets.size(); ++subset) {
        const auto& actual = geometry.subsets[subset];
        if (actual.index_start != std::stoul(fields[4 + subset * 3]) ||
            actual.index_count != std::stoul(fields[5 + subset * 3]) ||
            actual.material_slot != std::stoul(fields[6 + subset * 3])) return false;
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    std::ifstream manifest(argv[1]);
    if (!manifest) return 2;
    std::string line;
    int cases = 0;
    int failures = 0;
    while (std::getline(manifest, line)) {
        const std::vector<std::string> fields = split_tabs(line);
        if (fields.size() < 3) return 2;
        elisa::assets::CookedGeometry geometry;
        std::string error;
        const bool loaded = elisa::assets::load_cooked_geometry_asset(fields[1], geometry, error);
        bool passed = false;
        if (fields[0] == "accept") {
            passed = loaded && check_accepted(fields, geometry);
        } else if (fields[0] == "reject") {
            passed = !loaded && error.find(fields[2]) != std::string::npos;
        } else {
            return 2;
        }
        ++cases;
        if (!passed) {
            ++failures;
            std::fprintf(stderr, "subset case failed: %s %s (loaded=%d, error=%s)\n",
                fields[0].c_str(), fields[1].c_str(), loaded ? 1 : 0, error.c_str());
        }
    }
    if (cases == 0) return 2;
    std::printf("geometry subset loader: %d cases, %d failed\n", cases, failures);
    return failures == 0 ? 0 : 1;
}
