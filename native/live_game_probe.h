#pragma once
// Live game rendering: the native host parses a real SDL key event, maps it to
// the portable move code, advances the embedded Elisa game through the C ABI,
// and positions rendered state at the game's queried player cell, so the saved
// frame comes from gameplay this host itself advanced. The C ABI archive is
// emitted by scripts/wicked_probe.elisascript before this probe is compiled.
//
#include <SDL3/SDL.h>
#include "probe_core.h"
#include "probe_support.h"
#include "native_application.h"
#include "libmaze.h"
#include "png_capture.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

namespace probe {

inline int move_code_for_key(SDL_Keycode code) {
    if (code == SDLK_W) {
        return 0;
    }
    if (code == SDLK_S) {
        return 1;
    }
    if (code == SDLK_A) {
        return 2;
    }
    if (code == SDLK_D) {
        return 3;
    }
    return -1;
}

inline std::string live_screenshot_path(const char* screenshot_path) {
    const std::filesystem::path path(screenshot_path);
    return (path.parent_path() / (path.stem().string() + "-live" + path.extension().string())).string();
}

inline bool probe_live_game_rendering(wi::Application& application, wi::scene::Scene& scene,
                                      wi::ecs::Entity object, const char* screenshot_path) {
    if (!check(maze_start() == 1, "embedded game starts")) {
        return false;
    }
    SDL_Event event;
    std::memset(&event, 0, sizeof(event));
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.type = SDL_EVENT_KEY_DOWN;
    event.key.down = true;
    event.key.key = SDLK_D;
    event.key.scancode = SDL_GetScancodeFromKey(SDLK_D, nullptr);
    if (!check(SDL_PushEvent(&event), "live key event pushed")) {
        return false;
    }
    int move = -1;
    SDL_Event polled;
    while (SDL_PollEvent(&polled)) {
        if (polled.type == SDL_EVENT_KEY_DOWN) {
            move = move_code_for_key(polled.key.key);
        }
    }
    if (!check(move == 3, "live key maps to the east move")) {
        return false;
    }
    if (!check(maze_step(move) == 1, "live move advances the game")) {
        return false;
    }
    const int player_x = (int)maze_player_x();
    const int player_y = (int)maze_player_y();
    if (!check(player_x == 2 && player_y == 1, "live move reaches the expected cell")) {
        return false;
    }
    auto* transform = scene.transforms.GetComponent(object);
    if (!check(transform != nullptr, "live object transform")) {
        return false;
    }
    transform->translation_local = coordinates::cell_to_wicked(player_x, player_y);
    transform->SetDirty();
    transform->UpdateTransform();
    for (int frame = 0; frame < 5; ++frame) {
        application.Run();
        wi::helper::Sleep(50);
    }
    wi::graphics::GetDevice()->WaitForGPU();
    const wi::graphics::Texture presented = wi::graphics::GetDevice()->GetBackBuffer(&application.swapChain);
    if (!check(presented.IsValid(), "live presented frame")) {
        return false;
    }
    const std::string live_path = live_screenshot_path(screenshot_path);
    if (!check(save_rgba_png(presented, live_path), "live frame encode")) {
        return false;
    }
    std::fprintf(stdout, "wicked live game: key=d move=%d player=(%d,%d) frame=%s\n",
        move, player_x, player_y, live_path.c_str());
    return true;
}

inline int run_persistent_game(NativeApplication& host, wi::scene::Scene& scene,
                               wi::ecs::Entity object) {
    if (maze_start() != 1) {
        std::fprintf(stderr, "persistent host: game did not start\n");
        return 1;
    }
    bool paused = false;
    auto place_player = [&scene, object]() {
        auto* transform = scene.transforms.GetComponent(object);
        if (transform == nullptr) {
            return false;
        }
        transform->translation_local = coordinates::cell_to_wicked(maze_player_x(), maze_player_y());
        transform->SetDirty();
        transform->UpdateTransform();
        return true;
    };
    if (!place_player()) {
        return 1;
    }
    std::fprintf(stdout, "persistent host: W/A/S/D move, P pause/resume, R restart, close window to exit\n");
    while (host.poll_events([&](const SDL_Event& event) {
        if (event.type != SDL_EVENT_KEY_DOWN || !event.key.down) {
            return;
        }
        if (event.key.key == SDLK_P) {
            paused = !paused;
            std::fprintf(stdout, "persistent host: %s\n", paused ? "paused" : "resumed");
            return;
        }
        if (event.key.key == SDLK_R) {
            maze_start();
            place_player();
            std::fprintf(stdout, "persistent host: restarted\n");
            return;
        }
        if (!paused) {
            const int move = move_code_for_key(event.key.key);
            if (move >= 0 && maze_step(move) == 1) {
                place_player();
            }
        }
    })) {
        host.run_frame();
        wi::helper::Sleep(16);
    }
    std::fprintf(stdout, "persistent host: close requested\n");
    return 0;
}

} // namespace probe
