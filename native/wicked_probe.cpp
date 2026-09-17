#include "wiApplication.h"
#include "wiArguments.h"
#include "wiHelper.h"
#include "wiInitializer.h"
#include "wiRenderPath3D.h"
#include "wiRenderer.h"
#include "wiScene.h"
#include "wiVersion.h"
#include <SDL2/SDL.h>
#include <algorithm>
#include <chrono>
#include <SDL2/SDL_syswm.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace {
bool check(bool value, const char* message) {
    if (!value) {
        std::fprintf(stderr, "wicked probe failed: %s\n", message);
        return false;
    }
    return true;
}

bool load_manifest(const char* filename, std::map<std::string, std::string>& values) {
    std::ifstream input(filename);
    if (!input) {
        return false;
    }
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const auto separator = line.find('=');
        if (separator == std::string::npos) {
            return false;
        }
        values[line.substr(0, separator)] = line.substr(separator + 1);
    }
    return true;
}

// Elisa math is right-handed with +Y up and -Z forward (see
// src/math/geometry.elisa), and so is the Godot host. Wicked is a
// left-handed renderer, so its screen-right is Elisa's +X. Negating X at
// this boundary is the RH->LH conversion that keeps both hosts showing the
// same world instead of a mirrored one. Verified by sampling projected
// maze cells in both frames: without this the native image is the mirror
// of the Godot image.
XMFLOAT3 to_wicked_space(float x, float y, float z) {
    return XMFLOAT3(-x, y, z);
}

// "x,y;x,y;..." grid cells from the Elisa maze topology. Empty when the
// manifest has no wall line, which keeps single-cube hosts working.
std::vector<std::pair<int, int>> parse_walls(const std::string& spec) {
    std::vector<std::pair<int, int>> cells;
    size_t start = 0;
    while (start < spec.size()) {
        size_t end = spec.find(';', start);
        if (end == std::string::npos) {
            end = spec.size();
        }
        const std::string entry = spec.substr(start, end - start);
        const auto comma = entry.find(',');
        if (comma == std::string::npos) {
            return {};
        }
        cells.emplace_back(std::stoi(entry.substr(0, comma)), std::stoi(entry.substr(comma + 1)));
        start = end + 1;
    }
    return cells;
}

// A small unlit cube marking one game cell: player-facing evidence that the
// host draws the Elisa game's objects (key, door, hazards, goal), not just
// walls. Same RH->LH conversion as every other position.
wi::ecs::Entity create_cell_marker(wi::scene::Scene& scene, const std::string& name,
    int cell_x, int cell_y, float red, float green, float blue) {
    const auto entity = scene.Entity_CreateCube(name);
    if (entity == wi::ecs::INVALID_ENTITY) {
        return entity;
    }
    auto* transform = scene.transforms.GetComponent(entity);
    auto* material = scene.materials.GetComponent(entity);
    if (transform == nullptr) {
        return wi::ecs::INVALID_ENTITY;
    }
    transform->translation_local = to_wicked_space(
        (float)cell_x * 0.6f - 2.1f, (float)cell_y * 0.6f - 2.1f, 1.0f);
    transform->scale_local = XMFLOAT3(0.26f, 0.26f, 0.26f);
    transform->UpdateTransform();
    if (material != nullptr) {
        material->shaderType = wi::scene::MaterialComponent::SHADERTYPE_UNLIT;
        material->baseColor = XMFLOAT4(red, green, blue, 1.0f);
    }
    return entity;
}
}

int main(int argc, char** argv) {
    if (argc < 3 || argc > 5) {
        std::fprintf(stderr, "usage: wicked_probe <wicked-source-directory> <scene-manifest> [screenshot-png] [alwaysactive]\n");
        return 2;
    }
    // Forward flags such as "alwaysactive" to the engine argument table.
    // The hidden probe window is never active, so without "alwaysactive"
    // Application::Run returns before drawing a single pixel.
    wi::arguments::Parse(argc, argv);
    std::fprintf(stdout, "probe argc=%d alwaysactive=%d\n",
        argc, wi::arguments::HasArgument("alwaysactive") ? 1 : 0);
    std::map<std::string, std::string> manifest;
    if (!check(load_manifest(argv[2], manifest), "scene manifest")) {
        return 1;
    }
    if (!check(manifest["version"] == "1" && manifest["commands"] == "create,update,destroy", "scene manifest version")) {
        return 1;
    }
    const auto entity_id = std::stoll(manifest["entity"]);
    const auto camera_id = std::stoll(manifest["camera_entity"]);
    const auto width = std::stof(manifest["viewport_width"]);
    const auto height = std::stof(manifest["viewport_height"]);
    const auto object_x = std::stof(manifest["object_x"]);
    const auto object_y = std::stof(manifest["object_y"]);
    const auto object_z = std::stof(manifest["object_z"]);
    const auto camera_x = std::stof(manifest["camera_x"]);
    const auto camera_y = std::stof(manifest["camera_y"]);
    const auto camera_z = std::stof(manifest["camera_z"]);
    std::filesystem::current_path(argv[1]);
    const std::string shader_root = std::string(argv[1]) + "/shaders/";
    wi::renderer::SetShaderPath(shader_root);
    wi::renderer::SetShaderSourcePath(shader_root);
    std::fprintf(stdout, "wicked %s\n", wi::version::GetVersionString());
    if (!check(SDL_Init(SDL_INIT_VIDEO) == 0, "SDL video initialization")) {
        return 1;
    }
    SDL_Window* sdl_window = SDL_CreateWindow(
        "elisa-engine-probe", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        320, 200, SDL_WINDOW_HIDDEN | SDL_WINDOW_METAL
    );
    if (!check(sdl_window != nullptr, "hidden Metal window")) {
        SDL_Quit();
        return 1;
    }

    SDL_SysWMinfo window_info;
    SDL_VERSION(&window_info.version);
    if (!check(SDL_GetWindowWMInfo(sdl_window, &window_info), "Cocoa window handle")) {
        SDL_DestroyWindow(sdl_window);
        SDL_Quit();
        return 1;
    }

    wi::Application application;
    application.SetWindow(reinterpret_cast<SDL_Window*>(window_info.info.cocoa.window));
    application.Initialize();
    wi::initializer::WaitForInitializationsToFinish();

    wi::scene::Scene scene;
    const auto object = scene.Entity_CreateCube("elisa_cube_" + std::to_string(entity_id));
    const auto camera = scene.Entity_CreateCamera("elisa_camera_" + std::to_string(camera_id), width, height);
    // A point lamp: without any light the standard material shades black and
    // the captured frame carries no evidence that anything was drawn.
    const auto lamp = scene.Entity_CreateLight("elisa_light", XMFLOAT3(2.0f, 3.0f, -2.0f), XMFLOAT3(1.0f, 1.0f, 1.0f), 30.0f, 60.0f);
    if (!check(object != wi::ecs::INVALID_ENTITY, "cube entity") ||
        !check(camera != wi::ecs::INVALID_ENTITY, "camera entity") ||
        !check(lamp != wi::ecs::INVALID_ENTITY, "lamp entity")) {
        return 1;
    }

    // Maze wall geometry from the Elisa-owned topology. Rendered as an
    // unlit vertical wall map so the frame proves the native host draws
    // game-authored content, not a single test cube. The plane sits at
    // z=1 (6 units from the camera) where the whole 8x8 map fits the
    // default 45-degree frustum; the entity cube stays in front at z=3.
    //
    // Create every entity before borrowing any component pointer: adding
    // entities can reallocate the component stores, so pointers fetched
    // earlier would dangle.
    const auto walls_it = manifest.find("walls");
    const auto wall_cells = walls_it == manifest.end()
        ? std::vector<std::pair<int, int>>{}
        : parse_walls(walls_it->second);
    std::vector<wi::ecs::Entity> wall_entities;
    for (const auto& cell : wall_cells) {
        const auto wall = scene.Entity_CreateCube(
            "elisa_wall_" + std::to_string(cell.first) + "_" + std::to_string(cell.second));
        if (wall == wi::ecs::INVALID_ENTITY) {
            continue;
        }
        auto* wall_transform = scene.transforms.GetComponent(wall);
        auto* wall_material = scene.materials.GetComponent(wall);
        if (wall_transform == nullptr) {
            continue;
        }
        wall_transform->translation_local = to_wicked_space(
            (float)cell.first * 0.6f - 2.1f, (float)cell.second * 0.6f - 2.1f, 1.0f);
        wall_transform->scale_local = XMFLOAT3(0.3f, 0.3f, 0.3f);
        wall_transform->UpdateTransform();
        if (wall_material != nullptr) {
            wall_material->shaderType = wi::scene::MaterialComponent::SHADERTYPE_UNLIT;
            wall_material->baseColor = XMFLOAT4(0.8f, 0.8f, 0.85f, 1.0f);
        }
        wall_entities.push_back(wall);
    }
    std::fprintf(stdout, "wall cells=%u walls created=%u\n",
        (unsigned)wall_cells.size(), (unsigned)wall_entities.size());
    if (!wall_cells.empty() && wall_entities.size() != wall_cells.size()) {
        return 1;
    }

    // Game markers the Elisa rules place: key, door, hazards, goal. Zones
    // carry their own unlit colour so a captured frame can be checked for
    // the right object at the right cell.
    std::vector<wi::ecs::Entity> marker_entities;
    const auto marker_cells = [&manifest](const char* key) {
        const auto it = manifest.find(key);
        return it == manifest.end()
            ? std::vector<std::pair<int, int>>{}
            : parse_walls(it->second);
    };
    const struct { const char* field; const char* name; float r; float g; float b; } marker_specs[] = {
        {"goal", "elisa_goal", 0.1f, 0.9f, 0.2f},
        {"key", "elisa_key", 0.95f, 0.85f, 0.1f},
        {"door", "elisa_door", 0.85f, 0.2f, 0.9f},
        {"hazards", "elisa_hazard", 0.95f, 0.15f, 0.1f},
        {"hunter", "elisa_hunter", 1.0f, 0.55f, 0.1f},
    };
    for (const auto& spec : marker_specs) {
        for (const auto& cell : marker_cells(spec.field)) {
            const auto marker = create_cell_marker(scene,
                std::string(spec.name) + "_" + std::to_string(cell.first) + "_" + std::to_string(cell.second),
                cell.first, cell.second, spec.r, spec.g, spec.b);
            if (marker == wi::ecs::INVALID_ENTITY) {
                return 1;
            }
            marker_entities.push_back(marker);
        }
    }
    // Status indicator: the host draws the game's state as a coloured cell,
    // so a captured frame carries the status the Elisa game reported.
    {
        const auto status_it = manifest.find("game_status");
        const auto status_cell_it = manifest.find("status_cell");
        if (status_it != manifest.end() && status_cell_it != manifest.end()) {
            float status_r = 0.9f;
            float status_g = 0.9f;
            float status_b = 0.9f;
            const std::string status = status_it->second;
            if (status == "playing") { status_r = 0.1f; status_g = 0.85f; status_b = 0.9f; }
            else if (status == "paused") { status_r = 0.95f; status_g = 0.85f; status_b = 0.1f; }
            else if (status == "won") { status_r = 0.1f; status_g = 0.9f; status_b = 0.2f; }
            else if (status == "lost") { status_r = 0.95f; status_g = 0.15f; status_b = 0.1f; }
            for (const auto& cell : parse_walls(status_cell_it->second)) {
                const auto marker = create_cell_marker(scene,
                    "elisa_status_" + std::to_string(cell.first) + "_" + std::to_string(cell.second),
                    cell.first, cell.second, status_r, status_g, status_b);
                if (marker == wi::ecs::INVALID_ENTITY) {
                    return 1;
                }
                marker_entities.push_back(marker);
            }
            std::fprintf(stdout, "game status consumed=%s\n", status.c_str());
        }
    }
    std::fprintf(stdout, "markers created=%u\n", (unsigned)marker_entities.size());

    auto* object_transform = scene.transforms.GetComponent(object);
    auto* mesh = scene.meshes.GetComponent(object);
    auto* camera_transform = scene.transforms.GetComponent(camera);
    auto* camera_component = scene.cameras.GetComponent(camera);
    auto* lamp_transform = scene.transforms.GetComponent(lamp);
    auto* cube_material = scene.materials.GetComponent(object);
    if (!check(object_transform != nullptr, "cube transform") ||
        !check(mesh != nullptr && !mesh->subsets.empty(), "cube material subset") ||
        !check(camera_transform != nullptr && camera_component != nullptr, "camera components") ||
        !check(lamp_transform != nullptr, "lamp transform") ||
        !check(cube_material != nullptr, "cube material")) {
        return 1;
    }
    // Emissive sky-blue like the Godot probe albedo: if these pixels show,
    // rasterization works and only light transport is in question.
    cube_material->emissiveColor = XMFLOAT4(0.2f, 0.7f, 1.0f, 1.0f);
    // Unlit bypass in the same run: no lights, shadows, or exposure can
    // hide an unlit base color, so any pixels at all isolate the failure
    // to light transport versus fragment submission.
    cube_material->shaderType = wi::scene::MaterialComponent::SHADERTYPE_UNLIT;
    cube_material->baseColor = XMFLOAT4(0.2f, 0.7f, 1.0f, 1.0f);
    lamp_transform->translation_local = to_wicked_space(2.0f, 3.0f, -2.0f);
    lamp_transform->UpdateTransform();

    object_transform->translation_local = to_wicked_space(object_x, object_y, object_z);
    // One maze cell wide, so the player occupies exactly its own cell on the
    // marker plane instead of hiding neighbouring game objects.
    object_transform->scale_local = XMFLOAT3(0.3f, 0.3f, 0.3f);
    object_transform->UpdateTransform();
    const auto object_position = object_transform->GetPosition();
    if (!check(std::fabs(object_position.x - (-object_x)) < 0.0001f &&
        std::fabs(object_position.y - object_y) < 0.0001f &&
        std::fabs(object_position.z - object_z) < 0.0001f, "cube transform update")) {
        return 1;
    }
    std::fprintf(stdout, "cube world row=(%.2f,%.2f,%.2f,%.2f)\n",
        object_transform->world.m[3][0], object_transform->world.m[3][1],
        object_transform->world.m[3][2], object_transform->world.m[3][3]);
    camera_transform->translation_local = to_wicked_space(camera_x, camera_y, camera_z);
    // Default orientation already faces +Z toward the cube; leave it alone.
    camera_transform->UpdateTransform();
    camera_component->TransformCamera(*camera_transform);
    // TransformCamera only refreshes view matrices; the frustum used for
    // culling is rebuilt here, otherwise everything stays culled forever.
    camera_component->UpdateCamera();

    wi::RenderPath3D render_path;
    render_path.scene = &scene;
    render_path.camera = camera_component;
    // No occlusion queries in a one-shot probe: unprimed query heaps can
    // hold skips across frames with nothing ever proving visibility.
    render_path.setOcclusionCullingEnabled(false);
    wi::renderer::SetOcclusionCullingEnabled(false);
    application.ActivatePath(&render_path);
    std::fprintf(stdout, "pre-frames aabb=%u matrices=%u objects=%u\n",
        (unsigned)scene.aabb_objects.size(), (unsigned)scene.matrix_objects.size(),
        (unsigned)scene.objects.GetCount());
    // Pump platform events first: on macOS a window that never sees its
    // event queue may never finish mapping its Metal layer.
    for (int pump = 0; pump < 60; ++pump) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
        }
        wi::helper::Sleep(16);
    }
    // NOTE (depth experiment 2026-09-18): forcing the depth clear to 1.0
    // changed nothing versus the 0.0 default, so depth convention is not
    // the sole gate. The forcing lines were removed again to leave
    // engine state at defaults.
    // Settle loop: pipeline states compile in the background on first use
    // and draws using them are skipped until ready, so give the queue wall
    // time between frames instead of only counting frames.
    // Frame timing: the plan asks for measured frame time with median and
    // tail, not a "zero overhead" claim. These are hidden, trivial frames,
    // so the numbers are a floor for this scene, not a performance promise;
    // the runner still gates the median against the fixture's frame budget.
    // Warm-up frames are excluded on purpose: shader permutation creation and
    // history buffers make the first frames unrepresentative, and the plan
    // asks for steady-state median and tail, not start-up cost.
    const int warmup_frames = 5;
    const int measured_frames = 30;
    std::vector<int64_t> frame_micros;
    frame_micros.reserve(measured_frames);
    for (int frame = 0; frame < warmup_frames + measured_frames; ++frame) {
        const auto frame_start = std::chrono::steady_clock::now();
        application.Run();
        const auto frame_stop = std::chrono::steady_clock::now();
        if (frame >= warmup_frames) {
            frame_micros.push_back(std::chrono::duration_cast<std::chrono::microseconds>(frame_stop - frame_start).count());
        }
        wi::helper::Sleep(1000);
    }
    std::sort(frame_micros.begin(), frame_micros.end());
    const int64_t median_micros = frame_micros[frame_micros.size() / 2];
    const int64_t p95_micros = frame_micros[(frame_micros.size() * 95) / 100];
    const int64_t worst_micros = frame_micros.back();
    std::fprintf(stdout, "frame stats: samples=%u median_us=%lld p95_us=%lld worst_us=%lld\n",
        (unsigned)frame_micros.size(), (long long)median_micros, (long long)p95_micros, (long long)worst_micros);
    // Metal work is asynchronous: without draining the queue the backbuffer
    // still holds cleared memory when it is read below, no matter how many
    // frames were submitted.
    wi::graphics::GetDevice()->WaitForGPU();
    if (!check(render_path.GetRenderResult3D().IsValid(), "render target")) {
        return 1;
    }
    std::fprintf(stdout, "scene create/update/render passed\n");

    // Capture the rendered frame to PNG while the scene is still live. The
    // pixels are evidence, not authority: identity and lifecycle were
    // already established by Elisa-side checks above. A failed capture
    // fails the probe loudly instead of passing silently without pixels.
    const char* screenshot_path = argc >= 4 ? argv[3] : "wicked-frame.png";
    // Ground-truth diagnostics: eye/at, lamp/object positions, and the
    // visibility set the renderer actually computed. Pixels alone cannot
    // distinguish "camera faces away" from "light missing" from "culled".
    {
        const XMFLOAT3 eye = camera_component->Eye;
        const XMFLOAT3 at = camera_component->At;
        std::fprintf(stdout, "camera eye=(%.2f,%.2f,%.2f) at=(%.2f,%.2f,%.2f) wh=(%.1f,%.1f) near=%.3f far=%.1f fov=%.3f\n",
            eye.x, eye.y, eye.z, at.x, at.y, at.z,
            camera_component->width, camera_component->height,
            camera_component->zNearP, camera_component->zFarP, camera_component->fov);
        std::fprintf(stdout, "scene objects=%u lights=%u visible objects=%u visible lights=%u\n",
            (unsigned)scene.objects.GetCount(), (unsigned)scene.lights.GetCount(),
            (unsigned)render_path.visibility_main.visibleObjects.size(),
            (unsigned)render_path.visibility_main.visibleLights.size());
        std::fprintf(stdout, "aabb streams=%u\n", (unsigned)scene.aabb_objects.size());
        {
            XMUINT2 internal = render_path.GetInternalResolution();
            std::fprintf(stdout, "internal resolution=%ux%u\n", internal.x, internal.y);
        }
        std::fprintf(stdout, "mesh verts=%u idx-valid=%d pos-valid=%d indices=%u subset-count=%u general-valid=%d clu-valid=%d meshshader-allowed=%d\n",
            (unsigned)mesh->vertex_positions.size(),
            mesh->ib.IsValid() ? 1 : 0, mesh->vb_pos_wind.IsValid() ? 1 : 0,
            (unsigned)mesh->indices.size(),
            mesh->subsets.empty() ? 0u : (unsigned)mesh->subsets[0].indexCount,
            mesh->generalBuffer.IsValid() ? 1 : 0,
            mesh->vb_clu.IsValid() ? 1 : 0,
            wi::renderer::IsMeshShaderAllowed() ? 1 : 0);
        {
            const auto* drawable = scene.objects.GetComponent(object);
            std::fprintf(stdout, "object renderable=%d foreground=%d hide-main=%d filtermask=%u mesh-index=%u\n",
                (drawable != nullptr && drawable->IsRenderable()) ? 1 : 0,
                (drawable != nullptr && drawable->IsForeground()) ? 1 : 0,
                (drawable != nullptr && drawable->IsNotVisibleInMainCamera()) ? 1 : 0,
                drawable != nullptr ? drawable->GetFilterMask() : 0u,
                drawable != nullptr ? drawable->mesh_index : 9999u);
            if (!render_path.visibility_main.visibleObjects.empty()) {
                std::fprintf(stdout, "visible[0]=%u\n",
                    (unsigned)render_path.visibility_main.visibleObjects[0]);
            }
        }
        if (!scene.aabb_objects.empty()) {
            const auto& bounds = scene.aabb_objects[0];
            std::fprintf(stdout, "aabb0 min=(%.2f,%.2f,%.2f) max=(%.2f,%.2f,%.2f) layer=%u\n",
                bounds._min.x, bounds._min.y, bounds._min.z,
                bounds._max.x, bounds._max.y, bounds._max.z, bounds.layerMask);
        }
        std::fprintf(stdout, "instance matrices=%u\n", (unsigned)scene.matrix_objects.size());
        if (!scene.matrix_objects.empty()) {
            const auto& instance = scene.matrix_objects[0];
            std::fprintf(stdout, "instance0 row=(%.2f,%.2f,%.2f,%.2f)\n",
                instance.m[3][0], instance.m[3][1], instance.m[3][2], instance.m[3][3]);
        }
    }
    // Capture the composited swapchain image, not an intermediate target:
    // the postprocess result is only written when post effects run, so it
    // can sit stale while the presented frame is correct.
    const wi::graphics::Texture presented = wi::graphics::GetDevice()->GetBackBuffer(&application.swapChain);
    if (!check(presented.IsValid(), "presented frame")) {
        return 1;
    }
    if (!check(wi::helper::saveTextureToFile(presented, screenshot_path), "frame encode")) {
        return 1;
    }
    std::fprintf(stdout, "scene screenshot saved\n");
    {
        const std::filesystem::path stats_path = std::filesystem::path(screenshot_path).parent_path() / "frame-stats.txt";
        std::ofstream stats_out(stats_path);
        if (stats_out) {
            stats_out << "samples=" << frame_micros.size() << "\n"
                      << "median_us=" << median_micros << "\n"
                      << "p95_us=" << p95_micros << "\n"
                      << "worst_us=" << worst_micros << "\n";
        }
    }
    {
        // Render-target inventory: dimensions and sample counts pin down
        // the MSAA-resolve and sizing theories without downloading anything
        // the graphics API forbids reading back (see depth note below).
        std::fprintf(stdout, "rtMain %ux%u samples=%u msaa %ux%u samples=%u\n",
            render_path.rtMain.desc.width, render_path.rtMain.desc.height,
            render_path.rtMain.desc.sample_count,
            render_path.rtMain_render.desc.width, render_path.rtMain_render.desc.height,
            render_path.rtMain_render.desc.sample_count);
        {
            const wi::graphics::Texture* depth = render_path.GetDepthStencil();
            if (depth == nullptr || !depth->IsValid()) {
                std::fprintf(stdout, "depth target MISSING\n");
            } else {
                std::fprintf(stdout, "depth %ux%u fmt=%d\n",
                    depth->desc.width, depth->desc.height, (int)depth->desc.format);
            }
        }
        // NOTE: the depth target is intentionally never downloaded here:
        // blitting a Depth32Float_Stencil8 texture to a buffer trips
        // Metal API validation, so depth content is out of scope for
        // this probe. Validity and dimensions above are the whole claim.
    }

    scene.Entity_Remove(object);
    scene.Entity_Remove(camera);
    scene.Entity_Remove(lamp);
    // Despawn the whole maze wall set, then probe one representative for
    // the same object/mesh teardown guarantee the entity cube gets.
    for (const auto& wall : wall_entities) {
        scene.Entity_Remove(wall);
    }
    for (const auto& marker : marker_entities) {
        scene.Entity_Remove(marker);
    }
    if (!check(scene.objects.GetComponent(object) == nullptr, "cube despawn") ||
        !check(scene.meshes.GetComponent(object) == nullptr, "mesh despawn") ||
        !check(scene.cameras.GetComponent(camera) == nullptr, "camera despawn") ||
        !check(scene.lights.GetComponent(lamp) == nullptr, "lamp despawn") ||
        (!wall_entities.empty() &&
            !check(scene.objects.GetComponent(wall_entities.front()) == nullptr, "wall despawn"))) {
        return 1;
    }
    std::fprintf(stdout, "scene despawn passed\n");

    // Wicked's global worker systems have no public shutdown API. The one-shot
    // host exits after the lifecycle assertions so OS teardown cannot race them.
    std::fflush(stdout);
    std::_Exit(0);
}
