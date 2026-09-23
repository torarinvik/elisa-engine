#include "fbx_asset_import.h"
#include "mesh_tangent_frames.h"
#if defined(ELISA_TEST_COOKED_SKIN)
#include "cooked_geometry_package.h"
#endif

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

bool skin_bind_pose_test() {
    ufbx_scene scene{};
    ufbx_node root{}, rig{}, bone{};
    root.node_to_world = ufbx_identity_matrix;
    rig.parent = &root;
    rig.node_to_world = ufbx_identity_matrix;
    rig.node_to_world.m00 = rig.node_to_world.m11 = rig.node_to_world.m22 = 0.01;
    bone.parent = &rig;
    bone.node_to_world = rig.node_to_world;
    bone.node_to_world.m13 = 1.5; // Default node pose differs from skin binding.
    bone.local_transform = ufbx_identity_transform;
    bone.local_transform.translation.y = 150.0;
    ufbx_skin_cluster cluster{};
    cluster.bone_node = &bone;
    cluster.bind_to_world = rig.node_to_world;
    cluster.bind_to_world.m13 = 0.9;
    ufbx_skin_cluster* cluster_pointer = &cluster;
    ufbx_skin_deformer skin{};
    skin.clusters.data = &cluster_pointer;
    skin.clusters.count = 1;
    elisa::assets::FbxMeshData mesh;
    elisa::assets::FbxImportResult result;
    if (!check(elisa::assets::detail::append_skin_rig(scene, skin, mesh, result),
            "bind-pose fixture imports")) return false;
    return check(mesh.skin_joints.size() == 3 && mesh.skin_cluster_joints.size() == 1 &&
        std::abs(mesh.skin_joints[1].rest_local[7] - 0.01f) < 1e-6f &&
        std::abs(mesh.skin_joints[2].rest_local[1] - 90.0f) < 1e-4f,
        "skin rest hierarchy uses cluster bind matrices, including scaled ancestors");
}

bool tangent_normal_fallback_test() {
    const std::vector<float> positions = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f};
    const std::vector<float> missing_normals(9, 0.0f);
    const std::vector<float> uvs = {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f};
    const std::vector<uint32_t> indices = {0, 1, 2};
    std::vector<float> tangents;
    if (!check(elisa::assets::generate_tangent_frames(positions, missing_normals, uvs, indices, tangents),
            "zero source normals use generated geometric normals")) return false;
    bool finite = tangents.size() == 12;
    for (float value : tangents) finite = finite && std::isfinite(value);
    return check(finite && std::abs(tangents[0] - 1.0f) < 1.0e-5f &&
        std::abs(tangents[1]) < 1.0e-5f && std::abs(tangents[2]) < 1.0e-5f &&
        std::abs(tangents[3] - 1.0f) < 1.0e-5f,
        "fallback tangent frame is finite and follows the triangle UVs");
}

int fixture_test(const std::filesystem::path& path) {
    const auto asset = elisa::assets::import_fbx(path, true);
    if (!asset.ok) {
        std::fprintf(stderr, "FBX import failed: %s\n", asset.error.c_str());
        return 1;
    }
    print_summary("fixture", asset);
    bool ok = skin_bind_pose_test();
    ok &= tangent_normal_fallback_test();
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

int mesh_selection_test(const std::filesystem::path& path) {
    bool ok = true;
    const auto selected_mesh = elisa::assets::import_fbx(path, true, "SmallTriangle");
    ok &= check(selected_mesh.ok && selected_mesh.meshes == 2 && selected_mesh.triangles == 3 &&
        selected_mesh.primary_mesh.mesh_name == "SmallTriangle" &&
        selected_mesh.primary_mesh.indices.size() == 3,
        "exact mesh-name selection chooses a smaller mesh from a multi-mesh scene");
    const auto selected_node = elisa::assets::import_fbx(path, true, "SelectedNode");
    ok &= check(selected_node.ok && selected_node.primary_mesh.mesh_name == "SelectedQuad" &&
        selected_node.primary_mesh.indices.size() == 6,
        "exact node-name selection decodes that node's mesh");
    const auto missing = elisa::assets::import_fbx(path, true, "MissingMesh");
    ok &= check(!missing.ok && missing.error.find("selector did not match") != std::string::npos,
        "missing exact mesh names fail explicitly");
    const auto oversized = elisa::assets::import_fbx(path, true, std::string(513, 'x'));
    ok &= check(!oversized.ok && oversized.error.find("at most 512 bytes") != std::string::npos,
        "oversized mesh selectors are rejected before scene extraction");
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

#if defined(ELISA_TEST_COOKED_SKIN)
int cooked_skin_test(const std::filesystem::path& path) {
    elisa::assets::CookedGeometry geometry;
    std::string error;
    if (!elisa::assets::load_cooked_geometry(path.string(), geometry, error)) {
        std::fprintf(stderr, "cooked geometry load failed: %s\n", error.c_str());
        return 1;
    }
    bool ok = check(geometry.skin_bone_names.size() == 34,
        "cooked walking package retains its ordered 34-bone rig");
    ok &= check(geometry.skin_joints.size() >= geometry.skin_bone_names.size() &&
        geometry.skin_joints.size() <= 64 && geometry.skin_cluster_joints.size() == 34,
        "cooked walking package retains the bounded parent hierarchy and cluster map");
    ok &= check(geometry.animation_clips.size() == 1 &&
        geometry.animation_clips[0].name.find("Walking") != std::string::npos &&
        geometry.animation_clips[0].frame_count >= 20 && geometry.animation_clips[0].sample_rate == 30 &&
        geometry.animation_clips[0].local_transforms.size() ==
            geometry.skin_joints.size() * geometry.animation_clips[0].frame_count * 10,
        "cooked walking package retains normalized looping clip samples");
    bool has_motion = false;
    if (!geometry.animation_clips.empty() && !geometry.skin_joints.empty()) {
        const auto& clip = geometry.animation_clips[0];
        const size_t middle_frame = clip.frame_count / 2;
        for (size_t joint = 0; joint < geometry.skin_joints.size() && !has_motion; ++joint) {
            const size_t first = joint * 10;
            const size_t middle = (middle_frame * geometry.skin_joints.size() + joint) * 10;
            for (size_t component = 0; component < 10; ++component) {
                if (std::abs(clip.local_transforms[first + component] -
                    clip.local_transforms[middle + component]) > 1.0e-4f) has_motion = true;
            }
        }
    }
    ok &= check(has_motion, "walking clip samples contain changing joint poses");
    ok &= check(geometry.skin_indices.size() == geometry.positions.size() / 3 * 4 &&
        geometry.skin_weights.size() == geometry.skin_indices.size(),
        "cooked walking package has one four-influence row per vertex");
    for (size_t vertex = 0; vertex < geometry.skin_weights.size() / 4; ++vertex) {
        float total = 0.0f;
        for (size_t influence = 0; influence < 4; ++influence) {
            const size_t index = vertex * 4 + influence;
            ok &= check(geometry.skin_indices[index] < 34 && std::isfinite(geometry.skin_weights[index]) &&
                geometry.skin_weights[index] >= 0.0f, "cooked skin influence is valid");
            total += geometry.skin_weights[index];
        }
        ok &= check(std::abs(total - 1.0f) < 0.005f, "cooked vertex weights are normalized");
    }
    std::printf("cooked skin package: clusters=%zu joints=%zu clips=%zu vertices=%zu\n",
        geometry.skin_bone_names.size(), geometry.skin_joints.size(), geometry.animation_clips.size(),
        geometry.positions.size() / 3);
    return ok ? 0 : 1;
}
#endif

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
    const auto run_mesh = elisa::assets::import_fbx(running, true);
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
    ok &= check(walk_mesh.primary_mesh.skin_bone_names.size() == 34 &&
        walk_mesh.primary_mesh.skin_indices.size() == walk_mesh.primary_mesh.positions.size() / 3 * 4 &&
        walk_mesh.primary_mesh.skin_weights.size() == walk_mesh.primary_mesh.skin_indices.size(),
        "walking mesh preserves its ordered 34-bone skin payload");
    bool hierarchy_ordered = !walk_mesh.primary_mesh.skin_joints.empty() &&
        walk_mesh.primary_mesh.skin_joints.size() <= 64 &&
        walk_mesh.primary_mesh.skin_cluster_joints.size() == 34;
    for (size_t joint = 0; joint < walk_mesh.primary_mesh.skin_joints.size(); ++joint) {
        const auto& value = walk_mesh.primary_mesh.skin_joints[joint];
        hierarchy_ordered = hierarchy_ordered && value.parent_index < int32_t(joint) && value.parent_index >= -1;
    }
    for (size_t cluster = 0; cluster < walk_mesh.primary_mesh.skin_cluster_joints.size(); ++cluster) {
        const uint32_t joint = walk_mesh.primary_mesh.skin_cluster_joints[cluster];
        hierarchy_ordered = hierarchy_ordered && joint < walk_mesh.primary_mesh.skin_joints.size() &&
            walk_mesh.primary_mesh.skin_joints[joint].name == walk_mesh.primary_mesh.skin_bone_names[cluster];
    }
    ok &= check(hierarchy_ordered, "walking mesh preserves a parent-ordered rig and stable skin palette");
    ok &= check(walk_mesh.primary_mesh.animation_clips.size() == 1 &&
        walk_mesh.primary_mesh.animation_clips[0].name.find("Walking") != std::string::npos &&
        walk_mesh.primary_mesh.animation_clips[0].frame_count >= 20 &&
        walk_mesh.primary_mesh.animation_clips[0].local_transforms.size() ==
            walk_mesh.primary_mesh.skin_joints.size() * walk_mesh.primary_mesh.animation_clips[0].frame_count * 10,
        "walking FBX samples its valid clip and omits the zero-duration duplicate");
    ok &= check(run_mesh.ok && run_mesh.primary_mesh_extracted &&
        run_mesh.primary_mesh.skin_bone_names == walk_mesh.primary_mesh.skin_bone_names &&
        run_mesh.primary_mesh.skin_cluster_joints == walk_mesh.primary_mesh.skin_cluster_joints &&
        run_mesh.primary_mesh.skin_joints.size() == walk_mesh.primary_mesh.skin_joints.size() &&
        run_mesh.primary_mesh.animation_clips.size() == 1 &&
        run_mesh.primary_mesh.animation_clips[0].name.find("Running") != std::string::npos,
        "walking and running assets share the rig and expose their valid clips");
    bool valid_skin = walk_mesh.primary_mesh.skin_bone_names.size() == 34;
    for (size_t vertex = 0; vertex < walk_mesh.primary_mesh.skin_weights.size() / 4; ++vertex) {
        float sum = 0.0f;
        for (size_t influence = 0; influence < 4; ++influence) {
            const size_t index = vertex * 4 + influence;
            const uint32_t bone = walk_mesh.primary_mesh.skin_indices[index];
            const float weight = walk_mesh.primary_mesh.skin_weights[index];
            valid_skin = valid_skin && bone < 34 && std::isfinite(weight) && weight >= 0.0f;
            sum += weight;
        }
        valid_skin = valid_skin && std::abs(sum - 1.0f) < 0.001f;
    }
    ok &= check(valid_skin, "walking mesh has finite normalized four-bone influences");
    ok &= check(fence_asset.triangles > 3'000'000 && fence_asset.source_unit_meters > 0.0,
        "high-density fence source is imported within the configured limits");
    return ok ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--fixture") {
        return fixture_test(argv[2]);
    }
    if (argc == 3 && std::string(argv[1]) == "--mesh-selection") {
        return mesh_selection_test(argv[2]);
    }
    if (argc == 3 && std::string(argv[1]) == "--assets-root") {
        return supplied_assets_test(argv[2]);
    }
    if (argc == 3 && std::string(argv[1]) == "--decode") {
        return decode_mesh(argv[2]);
    }
#if defined(ELISA_TEST_COOKED_SKIN)
    if (argc == 3 && std::string(argv[1]) == "--cooked-skin") {
        return cooked_skin_test(argv[2]);
    }
    std::fprintf(stderr, "usage: fbx_asset_import_test --fixture FILE | --mesh-selection FILE | --assets-root DIR | --decode FILE | --cooked-skin FILE\n");
#else
    std::fprintf(stderr, "usage: fbx_asset_import_test --fixture FILE | --mesh-selection FILE | --assets-root DIR | --decode FILE\n");
#endif
    return 2;
}
