#include "cooked_geometry_package.h"
#include "cooked_collision_geometry.h"
#include "package_manifest.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace {
constexpr int FULL_FIXTURE_ARGUMENT_COUNT = 5;

bool replace_once(std::string& text, const std::string& from, const std::string& to) {
    const size_t found = text.find(from);
    if (found == std::string::npos) return false;
    text.replace(found, from.size(), to);
    return true;
}

bool collision_package_checks() {
    const std::string position_stream =
        "AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/";
    const std::string index_stream =
        "AAAAAAIAAAABAAAAAAAAAAEAAAADAAAAAAAAAAMAAAACAAAAAQAAAAIAAAADAAAA";
    const std::string header =
        "format=elisa-physics-collision-v1\nsource=test/tetra.gltf\n"
        "source_sha256=0000000000000000000000000000000000000000000000000000000000000000\n"
        "shape=convex_hull\nvertices=4\nindices=12\nposition_stride=12\nindex_stride=4\n";
    std::string valid = header + "positions_b64=" + position_stream +
        "\nindices_b64=" + index_stream + "\n";
    auto parse = [](const std::string& text, elisa::assets::CookedCollisionGeometry& geometry,
        std::string& error) {
        return elisa::assets::parse_cooked_collision_geometry_bytes(
            reinterpret_cast<const uint8_t*>(text.data()), text.size(), geometry, error);
    };
    elisa::assets::CookedCollisionGeometry geometry;
    std::string error;
    if (!parse(valid, geometry, error) || geometry.shape != "convex_hull" ||
        geometry.positions.size() != 12 || geometry.indices.size() != 12) return false;

    std::string oversized = valid;
    if (!replace_once(oversized, "vertices=4", "vertices=65537") ||
        parse(oversized, geometry, error)) return false;
    std::string bad_index = valid;
    if (!replace_once(bad_index, index_stream,
            "BAAAAAIAAAABAAAAAAAAAAEAAAADAAAAAAAAAAMAAAACAAAAAQAAAAIAAAADAAAA") ||
        parse(bad_index, geometry, error)) return false;
    std::string bad_hash = valid;
    if (!replace_once(bad_hash, std::string(64, '0'), std::string(64, 'z')) ||
        parse(bad_hash, geometry, error)) return false;

    const std::string flat_positions =
        "AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAACAPwAAgD8AAAAAAAAAAAAAgD8AAAAA";
    const std::string flat_indices = "AAAAAAEAAAACAAAAAAAAAAIAAAADAAAA";
    std::string flat_hull = valid;
    if (!replace_once(flat_hull, "vertices=4", "vertices=4") ||
        !replace_once(flat_hull, "indices=12", "indices=6") ||
        !replace_once(flat_hull, position_stream, flat_positions) ||
        !replace_once(flat_hull, index_stream, flat_indices) ||
        parse(flat_hull, geometry, error)) return false;
    if (!replace_once(flat_hull, "shape=convex_hull", "shape=triangle_mesh") ||
        !parse(flat_hull, geometry, error) || geometry.shape != "triangle_mesh" ||
        geometry.indices.size() != 6) return false;
    return true;
}
} // namespace

int main(int argc, char** argv) {
    if (!collision_package_checks()) return 14;
    if (argc != 2 && argc != FULL_FIXTURE_ARGUMENT_COUNT && argc != 6 && argc != 7) return 2;
    const probe::BinaryPackageIndex index = probe::read_binary_package_index(argv[1]);
    if (!index.valid || index.sections.size() < 2) return 3;
    const auto mesh = std::find_if(index.sections.begin(), index.sections.end(),
        [](const probe::BinaryPackageSection& section) { return section.name == "mesh"; });
    const auto texture = std::find_if(index.sections.begin(), index.sections.end(),
        [](const probe::BinaryPackageSection& section) { return section.name == "texture"; });
    if (mesh == index.sections.end() || mesh->compression > 1 || mesh->offset % 16 != 0) return 4;

    std::vector<uint8_t> texture_bytes;
    std::string error;
    if (texture != index.sections.end()) {
        if (texture->compression != 0 || texture->offset % 16 != 0 ||
            !probe::read_binary_package_section(argv[1], index, "texture", texture_bytes, error) ||
            texture_bytes.size() != 256) return 6;
        for (size_t index = 0; index < texture_bytes.size(); ++index) {
            if (texture_bytes[index] != index) return 7;
        }
    }

    elisa::assets::CookedGeometry geometry;
    if (!elisa::assets::load_cooked_geometry_asset(argv[1], geometry, error) ||
        geometry.positions.empty() || geometry.positions.size() % 3 != 0 ||
        geometry.normals.size() != geometry.positions.size() ||
        geometry.uvs.size() != geometry.positions.size() / 3 * 2 ||
        geometry.indices.empty() || geometry.indices.size() % 3 != 0) return 9;
    if (argc == FULL_FIXTURE_ARGUMENT_COUNT && geometry.positions.size() == 9 &&
        (geometry.positions[0] != 0.0f || geometry.positions[3] != 1.0f ||
            geometry.positions[7] != 1.0f || geometry.normals[2] != 1.0f ||
            geometry.indices != std::vector<uint32_t>{0, 1, 2})) return 13;

    const probe::BinaryPackageManifest manifest = probe::read_binary_package_manifest(argv[1]);
    const std::vector<std::string> expected_dependencies = texture == index.sections.end()
        ? std::vector<std::string>{}
        : std::vector<std::string>{"base.elpk", "foundation.elpk"};
    if (!manifest.valid || manifest.dependencies != expected_dependencies) return 8;

    if (argc == 6 || argc == 7) {
        elisa::assets::CookedCollisionGeometry collision;
        if (!elisa::assets::load_cooked_collision_geometry_asset(argv[5], collision, error) ||
            collision.shape != "triangle_mesh" || collision.positions.size() != 12 ||
            collision.indices.size() != 12) return 15;
    }
    if (argc == 7) {
        elisa::assets::CookedCollisionGeometry collision;
        if (!elisa::assets::load_cooked_collision_geometry_asset(argv[6], collision, error) ||
            collision.shape != "triangle_mesh" || collision.positions.size() != 12 ||
            collision.indices.size() != 12) return 16;
    }

    if (argc == FULL_FIXTURE_ARGUMENT_COUNT || argc == 6 || argc == 7) {
        const probe::BinaryPackageIndex many_sections = probe::read_binary_package_index(argv[2]);
        if (!many_sections.valid || many_sections.sections.size() != probe::PackageIndex::MAX_SECTIONS) return 10;
        const probe::PackageIndex many_legacy_sections = probe::read_package_index(argv[3]);
        if (!many_legacy_sections.valid || many_legacy_sections.sections.size() != 135) return 11;
        const probe::PackageIndex excess_legacy_sections = probe::read_package_index(argv[4]);
        if (excess_legacy_sections.valid || excess_legacy_sections.error != "invalid or duplicate section") return 12;
    }

    std::puts("Native ELPK reader accepted deterministic compressed bundle.");
    return 0;
}
