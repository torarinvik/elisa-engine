// Checks the production cooked-geometry loader against a manifest of packages.
// Each tab-separated manifest line is either
//   accept <path> <index count> <material slots> (<start> <count> <slot>)...
//          [materials (<10 factors> <alpha mode> <flags> <5 image references>)...]
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

bool check_accepted(const std::vector<std::string>& fields, const elisa::assets::CookedGeometry& geometry) {
    if (fields.size() < 4) return false;
    const auto marker = [&fields](const char* name) {
        return size_t(std::find(fields.begin() + 4, fields.end(), name) - fields.begin());
    };
    const size_t materials = marker("materials");
    const size_t sections = marker("sections");
    const size_t animations = marker("animations");
    const size_t morphs = marker("morphs");
    const size_t cameras = marker("cameras");
    const size_t lights = marker("lights");
    const size_t end = std::min({materials, sections, animations, morphs, cameras, lights});
    const size_t material_end = std::min({sections, animations, morphs, cameras, lights});
    const size_t section_end = std::min({animations, morphs, cameras, lights});
    if (materials == fields.size() ? !geometry.slot_materials.empty()
                                   : !check_slot_materials(fields, materials + 1, material_end, geometry)) return false;
    if (sections == fields.size() ? !geometry.texture_sections.empty() || !geometry.texture_checksums.empty()
                                  : !check_sections(fields, sections + 1, section_end, geometry)) return false;
    if (animations == fields.size() ? !geometry.animation_clips.empty()
                                    : geometry.animation_clips.size() != std::stoul(fields[animations + 1])) return false;
    if (morphs == fields.size() ? !geometry.morph_targets.empty()
                                : geometry.morph_targets.size() != std::stoul(fields[morphs + 1])) return false;
    if (cameras == fields.size() ? !geometry.cameras.empty()
                                 : geometry.cameras.size() != std::stoul(fields[cameras + 1])) return false;
    if (lights == fields.size() ? !geometry.lights.empty()
                                : geometry.lights.size() != std::stoul(fields[lights + 1])) return false;
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
