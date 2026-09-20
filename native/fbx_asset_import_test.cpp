#include "fbx_asset_import.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <utility>

namespace {

bool check(bool value, const char* label) {
    if (!value) std::fprintf(stderr, "FAIL: %s\n", label);
    return value;
}

void print_summary(const char* label, const elisa::assets::FbxImportResult& asset) {
    std::printf("%s: nodes=%llu meshes=%llu vertices=%llu triangles=%llu bones=%llu skins=%llu materials=%llu textures=%llu clips=%llu unit=%.6g clip=%s duration=%.6g\n",
        label,
        static_cast<unsigned long long>(asset.nodes),
        static_cast<unsigned long long>(asset.meshes),
        static_cast<unsigned long long>(asset.vertices),
        static_cast<unsigned long long>(asset.triangles),
        static_cast<unsigned long long>(asset.bones),
        static_cast<unsigned long long>(asset.skins),
        static_cast<unsigned long long>(asset.materials),
        static_cast<unsigned long long>(asset.texture_references),
        static_cast<unsigned long long>(asset.animation_stacks),
        asset.source_unit_meters, asset.first_clip_name.c_str(),
        asset.first_clip_duration_seconds);
}

int fixture_test(const std::filesystem::path& path) {
    const auto asset = elisa::assets::import_fbx(path, true);
    if (!asset.ok) {
        std::fprintf(stderr, "FBX import failed: %s\n", asset.error.c_str());
        return 1;
    }
    print_summary("fixture", asset);
    bool ok = true;
    ok &= check(asset.primary_mesh_extracted && asset.meshes == 1 && asset.triangles == 1,
        "triangle fixture scene and geometry were imported");
    ok &= check(asset.primary_mesh.indices.size() == 3 && asset.primary_mesh.positions.size() == 9 &&
        asset.primary_mesh.normals.size() == 9 && asset.primary_mesh.uvs.size() == 6,
        "normalized geometry channels have bounded matching sizes");
    ok &= check(std::abs(asset.source_unit_meters - 0.01) < 1.0e-8,
        "source unit metadata is preserved");
    ok &= check(std::abs(asset.primary_mesh.bounds_min[0] - 1.0f) < 1.0e-4f &&
        std::abs(asset.primary_mesh.bounds_max[0] - 2.0f) < 1.0e-4f &&
        std::abs(asset.primary_mesh.bounds_min[1]) < 1.0e-4f &&
        std::abs(asset.primary_mesh.bounds_max[1] - 1.0f) < 1.0e-4f,
        "node translation and centimetre-to-metre conversion reach normalized bounds");
    for (float value : asset.primary_mesh.positions) ok &= check(std::isfinite(value), "finite normalized positions");
    for (float value : asset.primary_mesh.normals) ok &= check(std::isfinite(value), "finite normalized normals");
    bool has_surface_normal = false;
    for (size_t index = 0; index + 2 < asset.primary_mesh.normals.size(); index += 3) {
        const float x = asset.primary_mesh.normals[index];
        const float y = asset.primary_mesh.normals[index + 1];
        const float z = asset.primary_mesh.normals[index + 2];
        has_surface_normal = has_surface_normal || x * x + y * y + z * z > 0.99f;
    }
    ok &= check(has_surface_normal, "missing source normals are generated for the triangle fixture");
    for (uint32_t index : asset.primary_mesh.indices) ok &= check(index < asset.primary_mesh.positions.size() / 3,
        "mesh index is in range");
    return ok ? 0 : 1;
}

int decode_mesh(const std::filesystem::path& path) {
    const auto asset = elisa::assets::import_fbx(path, true);
    if (!asset.ok) {
        std::fprintf(stderr, "FBX import failed: %s\n", asset.error.c_str());
        return 1;
    }
    print_summary("decoded", asset);
    std::printf("primary mesh node=%s mesh=%s indexed_vertices=%zu indices=%zu bounds=[%.4g,%.4g,%.4g]..[%.4g,%.4g,%.4g]\n",
        asset.primary_mesh.node_name.c_str(), asset.primary_mesh.mesh_name.c_str(),
        asset.primary_mesh.positions.size() / 3, asset.primary_mesh.indices.size(),
        asset.primary_mesh.bounds_min[0], asset.primary_mesh.bounds_min[1], asset.primary_mesh.bounds_min[2],
        asset.primary_mesh.bounds_max[0], asset.primary_mesh.bounds_max[1], asset.primary_mesh.bounds_max[2]);
    return 0;
}

int supplied_assets_test(const std::filesystem::path& root) {
    const std::filesystem::path walking = root / "walking anim" /
        "Meshy_AI_Blue_Sentinel_biped_Animation_Walking_withSkin.fbx";
    const std::filesystem::path running = root / "running anim" /
        "Meshy_AI_Blue_Sentinel_biped_Animation_Running_withSkin.fbx";
    const std::filesystem::path fence = root / "fence models" /
        "Meshy_AI_Industrial_Arc_Gate_0915101655_texture_fbx" /
        "Meshy_AI_Industrial_Arc_Gate_0915101655_texture.fbx";
    const auto walk_asset = elisa::assets::import_fbx(walking, false);
    const auto run_asset = elisa::assets::import_fbx(running, false);
    const auto fence_asset = elisa::assets::import_fbx(fence, false);
    const auto walk_mesh = elisa::assets::import_fbx(walking, true);
    for (const auto& pair : {std::pair<const char*, const elisa::assets::FbxImportResult*>{"walking", &walk_asset},
            {"running", &run_asset}, {"fence", &fence_asset}}) {
        if (!pair.second->ok) {
            std::fprintf(stderr, "%s FBX import failed: %s\n", pair.first, pair.second->error.c_str());
            return 1;
        }
        print_summary(pair.first, *pair.second);
    }
    bool ok = true;
    ok &= check(walk_asset.bones == 34 && walk_asset.skins > 0 &&
        walk_asset.triangles == 83522 && walk_asset.vertices == 41690 &&
        walk_asset.first_clip_name.size() >= 7 &&
        walk_asset.first_clip_name.compare(walk_asset.first_clip_name.size() - 7, 7, "Walking") == 0 &&
        walk_asset.first_clip_duration_seconds > 0.95 && walk_asset.first_clip_duration_seconds < 0.97,
        "walking FBX rig, clip and scene geometry match the source");
    ok &= check(run_asset.bones == 34 && run_asset.skins > 0 &&
        run_asset.bone_name_hash == walk_asset.bone_name_hash &&
        run_asset.first_clip_name.size() >= 7 &&
        run_asset.first_clip_name.compare(run_asset.first_clip_name.size() - 7, 7, "Running") == 0 &&
        run_asset.first_clip_duration_seconds > 0.62 && run_asset.first_clip_duration_seconds < 0.63,
        "running FBX shares the walking rig and exposes its clip");
    ok &= check(walk_mesh.ok && walk_mesh.primary_mesh_extracted &&
        walk_mesh.primary_mesh.mesh_name != "Icosphere" &&
        walk_mesh.primary_mesh.indices.size() == 83442 * 3,
        "largest cyborg mesh is selected without the auxiliary Icosphere");
    ok &= check(fence_asset.triangles > 3'000'000 && fence_asset.source_unit_meters > 0.0,
        "high-density fence source is imported within the configured limits");
    return ok ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--fixture") {
        return fixture_test(argv[2]);
    }
    if (argc == 3 && std::string(argv[1]) == "--assets-root") {
        return supplied_assets_test(argv[2]);
    }
    if (argc == 3 && std::string(argv[1]) == "--decode") {
        return decode_mesh(argv[2]);
    }
    std::fprintf(stderr, "usage: fbx_asset_import_test --fixture FILE | --assets-root DIR | --decode FILE\n");
    return 2;
}
