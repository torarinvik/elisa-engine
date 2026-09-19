#pragma once

// SDL3 boundary adapter for the portable action state machine. Device codes
// are translated here; gameplay sees only stable action IDs and edge state.
#include "probe_core.h"

#include <SDL3/SDL.h>

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace probe {

enum class ActionDevice : uint8_t { Keyboard, Mouse, Gamepad, Count };
enum class ActionContext : uint8_t { Gameplay, UI };

struct ActionBinding {
    int action = 0;
    ActionDevice device = ActionDevice::Keyboard;
    int code = 0;
    int chord_code = 0;
    float deadzone = 0.0f;
    ActionContext context = ActionContext::Gameplay;
};

class ActionInputBridge {
public:
    static constexpr size_t MAX_BINDINGS = 32;
    static constexpr size_t MAX_ACTIONS = 16;

    bool bind(ActionBinding binding) {
        if (binding.action <= 0 || binding.action > static_cast<int>(MAX_ACTIONS) ||
            binding.code <= 0 || binding.deadzone < 0.0f || binding.deadzone >= 1.0f) return false;
        for (const Binding& existing : bindings_) {
            if (existing.live && existing.value.action == binding.action &&
                existing.value.device == binding.device && existing.value.code == binding.code &&
                existing.value.context == binding.context) return false;
        }
        for (Binding& existing : bindings_) {
            if (!existing.live) {
                existing.value = binding;
                existing.live = true;
                return true;
            }
        }
        return false;
    }

    void set_context(ActionContext context) {
        context_ = context;
        for (State& state : states_) {
            state.down = false;
            state.value = 0.0f;
        }
    }

    void begin_frame() {
        for (State& state : states_) {
            state.pressed = false;
            state.released = false;
            state.value = 0.0f;
        }
    }

    void disconnect(ActionDevice device) {
        connected_[static_cast<size_t>(device)] = false;
        for (State& state : states_) {
            if (state.live) {
                state.down = false;
                state.released = true;
            }
        }
    }

    void reconnect(ActionDevice device) { connected_[static_cast<size_t>(device)] = true; }

    void apply(ActionDevice device, int code, float value, bool pressed, bool released, bool chord_down = false) {
        if (!connected_[static_cast<size_t>(device)]) return;
        for (const Binding& binding : bindings_) {
            const ActionBinding& item = binding.value;
            if (!binding.live || item.device != device || item.code != code || item.context != context_ ||
                (item.chord_code != 0 && !chord_down)) continue;
            const float magnitude = std::fabs(value) >= item.deadzone ? value : 0.0f;
            State& state = states_[static_cast<size_t>(item.action - 1)];
            state.live = true;
            if (std::fabs(magnitude) == 0.0f && !released) continue;
            state.value = std::max(std::fabs(magnitude), std::fabs(state.value)) == std::fabs(magnitude) ? magnitude : state.value;
            if (pressed && !state.down) state.pressed = true;
            state.down = pressed || (state.down && !released);
            if (released) {
                state.released = true;
                state.down = false;
            }
        }
    }

    void feed(const SDL_Event& event) {
        if (event.type == SDL_EVENT_KEY_DOWN) {
            apply(ActionDevice::Keyboard, static_cast<int>(event.key.key), 1.0f, true, false);
        } else if (event.type == SDL_EVENT_KEY_UP) {
            apply(ActionDevice::Keyboard, static_cast<int>(event.key.key), 0.0f, false, true);
        } else if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
            disconnect(ActionDevice::Keyboard);
            disconnect(ActionDevice::Mouse);
            disconnect(ActionDevice::Gamepad);
        }
    }

    bool down(int action) const { return valid_action(action) && states_[action - 1].down; }
    bool pressed(int action) const { return valid_action(action) && states_[action - 1].pressed; }
    bool released(int action) const { return valid_action(action) && states_[action - 1].released; }
    float value(int action) const { return valid_action(action) ? states_[action - 1].value : 0.0f; }

private:
    struct Binding { ActionBinding value; bool live = false; };
    struct State { bool down = false; bool pressed = false; bool released = false; float value = 0.0f; bool live = false; };
    bool valid_action(int action) const { return action > 0 && action <= static_cast<int>(MAX_ACTIONS); }

    std::array<Binding, MAX_BINDINGS> bindings_{};
    std::array<State, MAX_ACTIONS> states_{};
    std::array<bool, static_cast<size_t>(ActionDevice::Count)> connected_{{true, true, true}};
    ActionContext context_ = ActionContext::Gameplay;
};

inline bool probe_action_input_bridge() {
    ActionInputBridge input;
    if (!check(input.bind(ActionBinding{1, ActionDevice::Keyboard, SDLK_W, 0, 0.0f, ActionContext::Gameplay}),
        "action bridge binds keyboard")) return false;
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.type = SDL_EVENT_KEY_DOWN;
    event.key.key = SDLK_W;
    event.key.down = true;
    input.feed(event);
    if (!check(input.down(1) && input.pressed(1), "action bridge reads keyboard edge")) return false;
    input.begin_frame();
    event.type = SDL_EVENT_KEY_UP;
    event.key.type = SDL_EVENT_KEY_UP;
    event.key.down = false;
    input.feed(event);
    if (!check(input.released(1) && !input.down(1), "action bridge reads keyboard release")) return false;
    if (!check(input.bind(ActionBinding{2, ActionDevice::Gamepad, 10, 0, 0.2f, ActionContext::Gameplay}),
        "action bridge binds analog input")) return false;
    input.apply(ActionDevice::Gamepad, 10, 0.1f, true, false);
    if (!check(!input.down(2), "action bridge filters analog dead zone")) return false;
    input.apply(ActionDevice::Gamepad, 10, 0.8f, true, false);
    if (!check(input.down(2) && input.value(2) > 0.79f, "action bridge accepts analog input")) return false;
    event.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    input.feed(event);
    if (!check(input.released(2) && !input.down(2), "action bridge clears focus-lost state")) return false;
    input.reconnect(ActionDevice::Gamepad);
    return true;
}

} // namespace probe
