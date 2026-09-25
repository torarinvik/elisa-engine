#include "jolt_shape_cache.h"
#include "jolt_shape_cache_identity.h"
#include "physics_service_abi.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

int main() {
    namespace cache = elisa::physics::shape_cache;
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("elisa-jolt-cache-test-" + std::to_string(unique));
    const std::filesystem::path asset = root / "assets" / "mesh.elpk";
    std::error_code error;
    std::filesystem::create_directories(asset.parent_path(), error);
    if (error) return 1;
    { std::ofstream source(asset, std::ios::binary); source << "fixture"; }

    const ElisaCoordinateProfile profile = elisa_coordinate_profile();
    std::filesystem::path path;
    if (!cache::make_cache_path(root, asset, ELISA_PHYSICS_SHAPE_CONVEX_HULL,
            1.0f, 2.0f, 3.0f, profile, path)) return 2;
    const std::string source_digest = cache::digest(
        reinterpret_cast<const uint8_t*>("fixture"), 7);
    std::string key;
    if (!cache::make_cache_key(source_digest, ELISA_PHYSICS_SHAPE_CONVEX_HULL,
            1.0f, 2.0f, 3.0f, profile, 0x0102030405060708ull, key)) return 3;
    std::string changed_scale_key;
    if (!cache::make_cache_key(source_digest, ELISA_PHYSICS_SHAPE_CONVEX_HULL,
            1.0f, 2.0f, 4.0f, profile, 0x0102030405060708ull,
            changed_scale_key) || changed_scale_key == key) return 4;
    const std::string changed_source = cache::digest(
        reinterpret_cast<const uint8_t*>("fixture changed"), 15);
    std::string changed_source_key;
    if (!cache::make_cache_key(changed_source, ELISA_PHYSICS_SHAPE_CONVEX_HULL,
            1.0f, 2.0f, 3.0f, profile, 0x0102030405060708ull,
            changed_source_key) || changed_source_key == key) return 12;
    std::filesystem::path changed_scale_path;
    if (!cache::make_cache_path(root, asset, ELISA_PHYSICS_SHAPE_CONVEX_HULL,
            1.0f, 2.0f, 4.0f, profile, changed_scale_path) || changed_scale_path == path) return 13;

    const float positions[] = {
        -0.5f, 0.0f, -0.5f, 0.5f, 0.0f, -0.5f,
        0.0f, 0.0f, 0.5f, 0.0f, 1.0f, 0.0f,
    };
    const uint32_t indices[] = {0, 1, 3, 1, 2, 3, 2, 0, 3, 0, 2, 1};
    std::filesystem::path array_path, repeated_array_path;
    std::string array_key, repeated_array_key;
    if (!cache::make_array_geometry_identity(root, ELISA_PHYSICS_SHAPE_CONVEX_HULL,
            positions, 4, indices, 12, profile, 0x0102030405060708ull,
            array_path, array_key) ||
        !cache::make_array_geometry_identity(root, ELISA_PHYSICS_SHAPE_CONVEX_HULL,
            positions, 4, indices, 12, profile, 0x0102030405060708ull,
            repeated_array_path, repeated_array_key) || array_key != repeated_array_key ||
        array_path != repeated_array_path) return 15;
    float changed_positions[12];
    std::copy(std::begin(positions), std::end(positions), changed_positions);
    changed_positions[10] = 2.0f;
    std::filesystem::path changed_array_path;
    std::string changed_array_key;
    if (!cache::make_array_geometry_identity(root, ELISA_PHYSICS_SHAPE_CONVEX_HULL,
            changed_positions, 4, indices, 12, profile, 0x0102030405060708ull,
            changed_array_path, changed_array_key) || changed_array_key == array_key ||
        changed_array_path == array_path) return 16;
    uint32_t changed_indices[12];
    std::copy(std::begin(indices), std::end(indices), changed_indices);
    changed_indices[1] = 2;
    if (!cache::make_array_geometry_identity(root, ELISA_PHYSICS_SHAPE_CONVEX_HULL,
            positions, 4, changed_indices, 12, profile, 0x0102030405060708ull,
            changed_array_path, changed_array_key) || changed_array_key == array_key) return 18;
    if (!cache::make_array_geometry_identity(root, ELISA_PHYSICS_SHAPE_TRIANGLE_MESH,
            positions, 4, indices, 12, profile, 0x0102030405060708ull,
            changed_array_path, changed_array_key) || changed_array_key == array_key) return 17;
    ElisaCoordinateProfile changed_profile = profile;
    changed_profile.metres_per_unit *= 2.0f;
    if (!cache::make_array_geometry_identity(root, ELISA_PHYSICS_SHAPE_CONVEX_HULL,
            positions, 4, indices, 12, changed_profile, 0x0102030405060708ull,
            changed_array_path, changed_array_key) || changed_array_key == array_key ||
        !cache::make_array_geometry_identity(root, ELISA_PHYSICS_SHAPE_CONVEX_HULL,
            positions, 4, indices, 12, profile, 0x0102030405060709ull,
            changed_array_path, changed_array_key) || changed_array_key == array_key) return 19;

    const std::vector<uint8_t> payload = {0, 1, 2, 3, 0xfe, 0xff};
    if (!cache::store(path, key, ELISA_PHYSICS_SHAPE_CONVEX_HULL,
            0x0102030405060708ull, payload)) return 5;
    std::vector<uint8_t> restored;
    if (!cache::load(path, key, ELISA_PHYSICS_SHAPE_CONVEX_HULL,
            0x0102030405060708ull, restored) || restored != payload) return 6;
    if (cache::load(path, changed_source_key, ELISA_PHYSICS_SHAPE_CONVEX_HULL,
            0x0102030405060708ull, restored)) return 14;
    if (cache::load(path, changed_scale_key, ELISA_PHYSICS_SHAPE_CONVEX_HULL,
            0x0102030405060708ull, restored)) return 7;
    if (cache::load(path, key, ELISA_PHYSICS_SHAPE_TRIANGLE_MESH,
            0x0102030405060708ull, restored)) return 8;
    if (cache::load(path, key, ELISA_PHYSICS_SHAPE_CONVEX_HULL,
            0x0102030405060709ull, restored)) return 9;

    {
        std::fstream damaged(path, std::ios::binary | std::ios::in | std::ios::out);
        damaged.seekp(-1, std::ios::end);
        char byte = 0;
        damaged.write(&byte, 1);
    }
    if (cache::load(path, key, ELISA_PHYSICS_SHAPE_CONVEX_HULL,
            0x0102030405060708ull, restored)) return 10;

    std::filesystem::remove_all(root, error);
    return error ? 11 : 0;
}
