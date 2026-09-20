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
        for (Binding& binding : bindings_) {
            binding.down = false;
            binding.magnitude = 0.0f;
        }
        for (State& state : states_) {
            state.down = false;
            state.value = 0.0f;
        }
    }

    void begin_frame() {
        for (State& state : states_) {
            state.pressed = false;
            state.released = false;
        }
    }

    void disconnect(ActionDevice device) {
        connected_[static_cast<size_t>(device)] = false;
        clear_device_state(device);
    }

    void reconnect(ActionDevice device) { connected_[static_cast<size_t>(device)] = true; }

    // Focus loss clears transient state without marking physical devices
    // disconnected, so input can be accepted again when focus returns.
    void clear_device_state(ActionDevice device) {
        for (Binding& binding : bindings_) {
            if (binding.live && binding.value.device == device) {
                binding.down = false;
                binding.magnitude = 0.0f;
            }
        }
        refresh_all_actions();
    }

    void apply(ActionDevice device, int code, float value, bool pressed, bool released, bool chord_down = false) {
        if (!connected_[static_cast<size_t>(device)]) return;
        for (Binding& binding : bindings_) {
            const ActionBinding& item = binding.value;
            if (!binding.live || item.device != device || item.code != code || item.context != context_ ||
                (item.chord_code != 0 && !chord_down)) continue;
            const float magnitude = std::fabs(value) >= item.deadzone ? value : 0.0f;
            State& state = states_[static_cast<size_t>(item.action - 1)];
            state.live = true;
            if (std::fabs(magnitude) == 0.0f && !released) continue;
            binding.magnitude = released ? 0.0f : magnitude;
            if (released) binding.down = false;
            else if (pressed) binding.down = true;
            refresh_action(item.action);
        }
    }

    void feed(const SDL_Event& event) {
        if (event.type == SDL_EVENT_KEY_DOWN) {
            apply(ActionDevice::Keyboard, static_cast<int>(event.key.key), 1.0f, true, false);
        } else if (event.type == SDL_EVENT_KEY_UP) {
            apply(ActionDevice::Keyboard, static_cast<int>(event.key.key), 0.0f, false, true);
        } else if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
            clear_device_state(ActionDevice::Keyboard);
            clear_device_state(ActionDevice::Mouse);
            clear_device_state(ActionDevice::Gamepad);
        }
    }

    bool down(int action) const { return valid_action(action) && states_[action - 1].down; }
    bool pressed(int action) const { return valid_action(action) && states_[action - 1].pressed; }
    bool released(int action) const { return valid_action(action) && states_[action - 1].released; }
    float value(int action) const { return valid_action(action) ? states_[action - 1].value : 0.0f; }

private:
    struct Binding { ActionBinding value; bool live = false; bool down = false; float magnitude = 0.0f; };
    struct State { bool down = false; bool pressed = false; bool released = false; float value = 0.0f; bool live = false; };
    bool valid_action(int action) const { return action > 0 && action <= static_cast<int>(MAX_ACTIONS); }

    void refresh_action(int action) {
        if (!valid_action(action)) return;
        State& state = states_[static_cast<size_t>(action - 1)];
        bool down = false;
        float value = 0.0f;
        for (const Binding& binding : bindings_) {
            if (!binding.live || binding.value.action != action || binding.value.context != context_) continue;
            down = down || binding.down;
            if (std::fabs(binding.magnitude) > std::fabs(value)) value = binding.magnitude;
        }
        const bool was_down = state.down;
        state.down = down;
        state.value = value;
        if (!was_down && down) state.pressed = true;
        if (was_down && !down) state.released = true;
    }

    void refresh_all_actions() {
        for (int action = 1; action <= static_cast<int>(MAX_ACTIONS); ++action) refresh_action(action);
    }

    std::array<Binding, MAX_BINDINGS> bindings_{};
    std::array<State, MAX_ACTIONS> states_{};
    std::array<bool, static_cast<size_t>(ActionDevice::Count)> connected_{{true, true, true}};
    ActionContext context_ = ActionContext::Gameplay;
};

inline bool probe_action_input_bridge() {
    ActionInputBridge input;
    if (!check(input.bind(ActionBinding{1, ActionDevice::Keyboard, SDLK_W, 0, 0.0f, ActionContext::Gameplay}),
        "action bridge binds keyboard")) return false;
    if (!check(input.bind(ActionBinding{1, ActionDevice::Keyboard, SDLK_UP, 0, 0.0f, ActionContext::Gameplay}),
        "action bridge binds alternate key")) return false;
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.type = SDL_EVENT_KEY_DOWN;
    event.key.key = SDLK_W;
    event.key.down = true;
    input.feed(event);
    if (!check(input.down(1) && input.pressed(1), "action bridge reads keyboard edge")) return false;
    input.begin_frame();
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.type = SDL_EVENT_KEY_DOWN;
    event.key.key = SDLK_UP;
    event.key.down = true;
    input.feed(event);
    if (!check(input.down(1) && !input.pressed(1), "alternate binding keeps the action held")) return false;
    input.begin_frame();
    event.type = SDL_EVENT_KEY_UP;
    event.key.type = SDL_EVENT_KEY_UP;
    event.key.down = false;
    event.key.key = SDLK_W;
    input.feed(event);
    if (!check(input.down(1) && !input.released(1), "releasing one binding keeps the action down")) return false;
    input.begin_frame();
    event.key.key = SDLK_UP;
    input.feed(event);
    if (!check(input.released(1) && !input.down(1), "action releases when its final binding releases")) return false;
    if (!check(input.bind(ActionBinding{2, ActionDevice::Gamepad, 10, 0, 0.2f, ActionContext::Gameplay}),
        "action bridge binds analog input")) return false;
    input.apply(ActionDevice::Gamepad, 10, 0.1f, true, false);
    if (!check(!input.down(2), "action bridge filters analog dead zone")) return false;
    input.apply(ActionDevice::Gamepad, 10, 0.8f, true, false);
    if (!check(input.down(2) && input.value(2) > 0.79f, "action bridge accepts analog input")) return false;
    input.begin_frame();
    if (!check(input.down(2) && input.value(2) > 0.79f, "action bridge preserves held value across frames")) return false;
    event.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    input.feed(event);
    if (!check(input.released(2) && !input.down(2), "action bridge clears focus-lost state")) return false;
    input.begin_frame();
    input.apply(ActionDevice::Gamepad, 10, 0.8f, true, false);
    if (!check(input.down(2) && input.value(2) > 0.79f, "focus loss leaves the gamepad connected")) return false;
    if (!check(input.bind(ActionBinding{3, ActionDevice::Keyboard, SDLK_A, 0, 0.0f, ActionContext::Gameplay}),
        "action bridge binds disconnect-test keyboard")) return false;
    if (!check(input.bind(ActionBinding{3, ActionDevice::Gamepad, 11, 0, 0.0f, ActionContext::Gameplay}),
        "action bridge binds disconnect-test gamepad")) return false;
    input.apply(ActionDevice::Keyboard, SDLK_A, 1.0f, true, false);
    input.apply(ActionDevice::Gamepad, 11, 0.6f, true, false);
    input.disconnect(ActionDevice::Keyboard);
    if (!check(input.down(3) && !input.released(3) && input.value(3) > 0.59f,
        "disconnecting one device preserves another binding")) return false;
    input.apply(ActionDevice::Keyboard, SDLK_A, 1.0f, true, false);
    if (!check(input.down(3), "events from a disconnected device are ignored")) return false;
    input.apply(ActionDevice::Gamepad, 11, 0.0f, false, true);
    if (!check(input.released(3) && !input.down(3), "remaining device release ends the action")) return false;
    return true;
}

} // namespace probe
