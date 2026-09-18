#pragma once
// Host input path: SDL keyboard events are translated to the portable button
// names the engine's input map uses, never to enum ordinals. The probe pushes
// synthetic key events for the fixture's bound keys, polls them back, and
// requires each to map to the named portable button. It exercises the platform
// boundary; gameplay action selection still runs in Elisa.
//
// This probe is compiled into the Wicked host, whose upstream platform layer
// builds against SDL2, so it must use SDL2: SDL2 and SDL3 export the same
// symbol names and cannot link into one binary. The engine's own platform
// default is SDL3 (src/backend/sdl3.elisa and the standalone embedding host).
#include "probe_core.h"

#include <SDL2/SDL.h>

#include <cstdio>
#include <cstring>
#include <map>
#include <string>

namespace probe {

inline SDL_Keycode sdl_key_for(const std::string& name) {
    if (name == "KeyW") {
        return SDLK_w;
    }
    if (name == "KeyA") {
        return SDLK_a;
    }
    if (name == "KeyD") {
        return SDLK_d;
    }
    return SDLK_UNKNOWN;
}

inline std::string portable_button_for(SDL_Keycode code) {
    if (code == SDLK_w) {
        return "KeyW";
    }
    if (code == SDLK_a) {
        return "KeyA";
    }
    if (code == SDLK_d) {
        return "KeyD";
    }
    return "Unbound";
}

inline bool probe_input(const std::map<std::string, std::string>& manifest) {
    const auto forward = manifest.find("input_forward");
    const auto left = manifest.find("input_left");
    const auto right = manifest.find("input_right");
    if (forward == manifest.end() || left == manifest.end() || right == manifest.end()) {
        return true;
    }
    const std::string expected[3] = { forward->second, left->second, right->second };
    int delivered = 0;
    for (const std::string& name : expected) {
        const SDL_Keycode code = sdl_key_for(name);
        if (!check(code != SDLK_UNKNOWN, "input key name is known")) {
            return false;
        }
        SDL_Event event;
        std::memset(&event, 0, sizeof(event));
        event.type = SDL_KEYDOWN;
        event.key.type = SDL_KEYDOWN;
        event.key.keysym.sym = code;
        event.key.keysym.scancode = SDL_GetScancodeFromKey(code);
        if (!check(SDL_PushEvent(&event) == 1, "input event pushed")) {
            return false;
        }
        bool matched = false;
        SDL_Event polled;
        while (SDL_PollEvent(&polled) == 1) {
            if (polled.type == SDL_KEYDOWN) {
                matched = portable_button_for(polled.key.keysym.sym) == name;
            }
        }
        if (!check(matched, "input event maps to the portable button name")) {
            return false;
        }
        ++delivered;
    }
    std::fprintf(stdout, "input: forward=%s left=%s right=%s events=%d\n",
        forward->second.c_str(), left->second.c_str(), right->second.c_str(), delivered);
    return true;
}

} // namespace probe
