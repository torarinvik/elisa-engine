#include "wiApplication.h"
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
    if (argc != 3) {
        std::fprintf(stderr, "usage: wicked_probe <wicked-source-directory> <scene-manifest>\n");
        return 2;
    }
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
    if (!check(object != wi::ecs::INVALID_ENTITY, "cube entity") ||
        !check(camera != wi::ecs::INVALID_ENTITY, "camera entity")) {
        return 1;
    }

    auto* object_transform = scene.transforms.GetComponent(object);
    auto* mesh = scene.meshes.GetComponent(object);
    auto* camera_transform = scene.transforms.GetComponent(camera);
    auto* camera_component = scene.cameras.GetComponent(camera);
    if (!check(object_transform != nullptr, "cube transform") ||
        !check(mesh != nullptr && !mesh->subsets.empty(), "cube material subset") ||
        !check(camera_transform != nullptr && camera_component != nullptr, "camera components")) {
        return 1;
    }

    object_transform->translation_local = XMFLOAT3(object_x, object_y, object_z);
    object_transform->UpdateTransform();
    const auto object_position = object_transform->GetPosition();
    if (!check(std::fabs(object_position.x - object_x) < 0.0001f &&
        std::fabs(object_position.y - object_y) < 0.0001f &&
        std::fabs(object_position.z - object_z) < 0.0001f, "cube transform update")) {
        return 1;
    }
    camera_transform->translation_local = XMFLOAT3(camera_x, camera_y, camera_z);
    camera_transform->UpdateTransform();
    camera_component->TransformCamera(*camera_transform);

    wi::RenderPath3D render_path;
    render_path.scene = &scene;
    render_path.camera = camera_component;
    application.ActivatePath(&render_path);
    application.Run();
    if (!check(render_path.GetRenderResult3D().IsValid(), "render target")) {
        return 1;
    }
    std::fprintf(stdout, "scene create/update/render passed\n");

    scene.Entity_Remove(object);
    scene.Entity_Remove(camera);
    if (!check(scene.objects.GetComponent(object) == nullptr, "cube despawn") ||
        !check(scene.meshes.GetComponent(object) == nullptr, "mesh despawn") ||
        !check(scene.cameras.GetComponent(camera) == nullptr, "camera despawn")) {
        return 1;
    }
    std::fprintf(stdout, "scene despawn passed\n");

    // Wicked's global worker systems have no public shutdown API. The one-shot
    // host exits after the lifecycle assertions so OS teardown cannot race them.
    std::fflush(stdout);
    std::_Exit(0);
}
