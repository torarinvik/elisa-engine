#include "cooked_geometry_package.h"
#include "lod_manifest.h"

#include <cstdio>
#include <filesystem>
#include <string>

int main(int argc, char** argv) {
    if (argc < 3) return 2;
    const std::filesystem::path manifest_path(argv[1]);
    const elisa::assets::LodManifest manifest = elisa::assets::read_lod_manifest(argv[1]);
    if (!manifest.valid || manifest.level_count < 2) {
        std::fprintf(stderr, "valid LOD manifest rejected: %s\n", manifest.error.c_str());
        return 1;
    }
    for (size_t index = 0; index < manifest.level_count; ++index) {
        const auto& level = manifest.levels[index];
        const std::filesystem::path package_path = manifest_path.parent_path() / level.package;
        std::error_code status;
        const uint64_t byte_size = std::filesystem::file_size(package_path, status);
        elisa::assets::CookedGeometry geometry;
        std::string error;
        if (status || byte_size != level.byte_size ||
            !elisa::assets::load_cooked_geometry_asset(package_path.string(), geometry, error) ||
            geometry.indices.size() / 3 != level.triangles ||
            geometry.positions.size() / 3 != level.vertices) {
            std::fprintf(stderr, "LOD package rejected: %s (%s)\n", level.package.c_str(), error.c_str());
            return 1;
        }
    }
    for (int index = 2; index < argc; ++index) {
        const auto rejected = elisa::assets::read_lod_manifest(argv[index]);
        if (rejected.valid) {
            std::fprintf(stderr, "malformed LOD manifest accepted: %s\n", argv[index]);
            return 1;
        }
    }
    std::printf("LOD manifest reader: %zu levels loaded; %d malformed variants rejected\n",
        manifest.level_count, argc - 2);
    return 0;
}
