#pragma once
// Live game rendering: the native host parses a real SDL key event, maps it to
// the portable move code, advances the embedded Elisa game through the C ABI,
// and positions rendered state at the game's queried player cell, so the saved
// frame comes from gameplay this host itself advanced. The C ABI archive is
// emitted by scripts/wicked_probe.elisascript before this probe is compiled.
//
// This file is compiled into the Wicked host, whose upstream platform layer
// builds against SDL2, so it uses SDL2 events. SDL2 and SDL3 export the same
// symbols and cannot link into one binary; the engine's own default is SDL3
// (src/backend/sdl3.elisa and the standalone embedding host).
#include "probe_core.h"
#include "probe_support.h"
#include "libmaze.h"
#include "png_capture.h"

#include <SDL2/SDL.h>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

namespace probe {

inline int move_code_for_key(SDL_Keycode code) {
    if (code == SDLK_w) {
        return 0;
    }
    if (code == SDLK_s) {
        return 1;
    }
    if (code == SDLK_a) {
        return 2;
    }
    if (code == SDLK_d) {
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
    event.type = SDL_KEYDOWN;
    event.key.type = SDL_KEYDOWN;
    event.key.state = SDL_PRESSED;
    event.key.keysym.sym = SDLK_d;
    event.key.keysym.scancode = SDL_GetScancodeFromKey(SDLK_d);
    if (!check(SDL_PushEvent(&event) == 1, "live key event pushed")) {
        return false;
    }
    int move = -1;
    SDL_Event polled;
    while (SDL_PollEvent(&polled) == 1) {
        if (polled.type == SDL_KEYDOWN) {
            move = move_code_for_key(polled.key.keysym.sym);
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
    transform->translation_local = to_wicked_space(
        (float)player_x * 0.6f - 2.1f, (float)player_y * 0.6f - 2.1f, 1.0f);
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

} // namespace probe
