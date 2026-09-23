#include "cooked_geometry_package.h"
#include "lod_geometry_chain.h"
#include "sha256_file.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

int main(int argc, char** argv) {
    if (argc < 3) return 2;
    const std::filesystem::path manifest_path(argv[1]);
    if (setenv("ELISA_PROJECT_ROOT", manifest_path.parent_path().string().c_str(), 1) != 0) return 2;
    elisa::assets::Sha256 known_hash;
    static constexpr uint8_t abc[] = {'a', 'b', 'c'};
    known_hash.update(abc, sizeof(abc));
    if (known_hash.finish() != "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") {
        std::fprintf(stderr, "SHA-256 known vector failed\n");
        return 1;
    }
    elisa::assets::LodGeometryChain chain;
    if (!elisa::assets::load_lod_geometry_chain(manifest_path.filename().generic_string(), chain) ||
        chain.level_count < 2) {
        std::fprintf(stderr, "valid LOD chain rejected: %s\n", chain.error.c_str());
        return 1;
    }
    size_t rejected_count = 0;
    for (int index = 2; index < argc; ++index) {
        if (std::string(argv[index]) == "--reject-chain") {
            if (++index >= argc) return 2;
            elisa::assets::LodGeometryChain rejected;
            if (elisa::assets::load_lod_geometry_chain(
                    std::filesystem::path(argv[index]).filename().generic_string(), rejected)) {
                std::fprintf(stderr, "invalid LOD package chain accepted: %s\n", argv[index]);
                return 1;
            }
            ++rejected_count;
            continue;
        }
        const auto rejected = elisa::assets::read_lod_manifest(argv[index]);
        if (rejected.valid) {
            std::fprintf(stderr, "malformed LOD manifest accepted: %s\n", argv[index]);
            return 1;
        }
        ++rejected_count;
    }
    std::printf("LOD chain loader: %zu levels hash-verified; %zu malformed manifests/packages rejected\n",
        chain.level_count, rejected_count);
    return 0;
}
