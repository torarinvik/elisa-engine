#include "wiApplication.h"
#include "wiArguments.h"
#include "wiHelper.h"
#include "wiInitializer.h"
#include "wiRenderPath3D.h"
#include "wiRenderer.h"
#include "wiScene.h"
#include "wiVersion.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

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
    lamp_transform->translation_local = XMFLOAT3(2.0f, 3.0f, -2.0f);
    lamp_transform->UpdateTransform();

    object_transform->translation_local = XMFLOAT3(object_x, object_y, object_z);
    object_transform->UpdateTransform();
    const auto object_position = object_transform->GetPosition();
    if (!check(std::fabs(object_position.x - object_x) < 0.0001f &&
        std::fabs(object_position.y - object_y) < 0.0001f &&
        std::fabs(object_position.z - object_z) < 0.0001f, "cube transform update")) {
        return 1;
    }
    camera_transform->translation_local = XMFLOAT3(camera_x, camera_y, camera_z);
    // Default orientation already faces +Z toward the cube; leave it alone.
    camera_transform->UpdateTransform();
    camera_component->TransformCamera(*camera_transform);
    // TransformCamera only refreshes view matrices; the frustum used for
    // culling is rebuilt here, otherwise everything stays culled forever.
    camera_component->UpdateCamera();

    wi::RenderPath3D render_path;
    render_path.scene = &scene;
    render_path.camera = camera_component;
    application.ActivatePath(&render_path);
    // Several frames: the first frames after path activation still warm
    // up async shader compilation and postprocess history. Runtime shader
    // compiles take seconds, so a handful of fast frames can all run
    // before the object shaders exist and every draw is skipped.
    // Pump platform events first: on macOS a window that never sees its
    // event queue may never finish mapping its Metal layer.
    for (int pump = 0; pump < 60; ++pump) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
        }
        wi::helper::Sleep(16);
    }
    for (int frame = 0; frame < 30; ++frame) {
        application.Run();
    }
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
        std::fprintf(stdout, "camera eye=(%.2f,%.2f,%.2f) at=(%.2f,%.2f,%.2f)\n",
            eye.x, eye.y, eye.z, at.x, at.y, at.z);
        std::fprintf(stdout, "scene objects=%u lights=%u visible objects=%u visible lights=%u\n",
            (unsigned)scene.objects.GetCount(), (unsigned)scene.lights.GetCount(),
            (unsigned)render_path.visibility_main.visibleObjects.size(),
            (unsigned)render_path.visibility_main.visibleLights.size());
        std::fprintf(stdout, "aabb streams=%u\n", (unsigned)scene.aabb_objects.size());
        {
            XMUINT2 internal = render_path.GetInternalResolution();
            std::fprintf(stdout, "internal resolution=%ux%u\n", internal.x, internal.y);
        }
        std::fprintf(stdout, "mesh verts=%u idx-valid=%d pos-valid=%d indices=%u subset-count=%u\n",
            (unsigned)mesh->vertex_positions.size(),
            mesh->ib.IsValid() ? 1 : 0, mesh->vb_pos_wind.IsValid() ? 1 : 0,
            (unsigned)mesh->indices.size(),
            mesh->subsets.empty() ? 0u : (unsigned)mesh->subsets[0].indexCount);
        {
            const auto* drawable = scene.objects.GetComponent(object);
            std::fprintf(stdout, "object renderable=%d foreground=%d hide-main=%d filtermask=%u\n",
                (drawable != nullptr && drawable->IsRenderable()) ? 1 : 0,
                (drawable != nullptr && drawable->IsForeground()) ? 1 : 0,
                (drawable != nullptr && drawable->IsNotVisibleInMainCamera()) ? 1 : 0,
                drawable != nullptr ? drawable->GetFilterMask() : 0u);
        }
        if (!scene.aabb_objects.empty()) {
            const auto& bounds = scene.aabb_objects[0];
            std::fprintf(stdout, "aabb0 min=(%.2f,%.2f,%.2f) max=(%.2f,%.2f,%.2f) layer=%u\n",
                bounds._min.x, bounds._min.y, bounds._min.z,
                bounds._max.x, bounds._max.y, bounds._max.z, bounds.layerMask);
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
        // Temporary diagnostic: is the scene landing in the MSAA target
        // while the resolve never runs?
        wi::vector<uint8_t> msaa_png;
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
        if (wi::helper::saveTextureToMemoryFile(*render_path.GetDepthStencil(), "PNG", msaa_png)) {
            std::ofstream msaa_out("/tmp/wicked-depth.png", std::ios::binary);
            msaa_out.write((const char*)msaa_png.data(), (std::streamsize)msaa_png.size());
        }
    }

    scene.Entity_Remove(object);
    scene.Entity_Remove(camera);
    scene.Entity_Remove(lamp);
    if (!check(scene.objects.GetComponent(object) == nullptr, "cube despawn") ||
        !check(scene.meshes.GetComponent(object) == nullptr, "mesh despawn") ||
        !check(scene.cameras.GetComponent(camera) == nullptr, "camera despawn") ||
        !check(scene.lights.GetComponent(lamp) == nullptr, "lamp despawn")) {
        return 1;
    }
    std::fprintf(stdout, "scene despawn passed\n");

    // Wicked's global worker systems have no public shutdown API. The one-shot
    // host exits after the lifecycle assertions so OS teardown cannot race them.
    std::fflush(stdout);
    std::_Exit(0);
}
