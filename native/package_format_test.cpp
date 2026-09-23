#include "cooked_geometry_package.h"
#include "package_manifest.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 2 && argc != 5) return 2;
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

    const probe::BinaryPackageManifest manifest = probe::read_binary_package_manifest(argv[1]);
    const std::vector<std::string> expected_dependencies = texture == index.sections.end()
        ? std::vector<std::string>{}
        : std::vector<std::string>{"base.elpk", "foundation.elpk"};
    if (!manifest.valid || manifest.dependencies != expected_dependencies) return 8;

    if (argc == 5) {
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
