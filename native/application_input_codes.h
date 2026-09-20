#pragma once

#include "application_abi.h"
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_keycode.h>

#include <cstdint>

namespace probe {

enum ElisaInputDeviceCode : int32_t {
    INPUT_DEVICE_GLOBAL = -1,
    INPUT_DEVICE_KEYBOARD = 0,
    INPUT_DEVICE_MOUSE = 1,
    INPUT_DEVICE_GAMEPAD = 2,
};

constexpr uint32_t INPUT_TOKEN_KIND_BITS = 4;
constexpr uint32_t INPUT_TOKEN_DEVICE_BITS = 2;
constexpr uint32_t INPUT_TOKEN_AXIS_CODE_BITS = 12;
constexpr uint32_t INPUT_TOKEN_AXIS_VALUE_BITS = 20;
constexpr uint32_t INPUT_TOKEN_DEVICE_SHIFT = INPUT_TOKEN_KIND_BITS;
constexpr uint32_t INPUT_TOKEN_PAYLOAD_SHIFT = INPUT_TOKEN_KIND_BITS + INPUT_TOKEN_DEVICE_BITS;
constexpr uint32_t INPUT_TOKEN_AXIS_CODE_SHIFT = INPUT_TOKEN_PAYLOAD_SHIFT + INPUT_TOKEN_AXIS_VALUE_BITS;
constexpr uint32_t INPUT_TOKEN_PRESSED_SHIFT = INPUT_TOKEN_AXIS_CODE_SHIFT + INPUT_TOKEN_AXIS_CODE_BITS;
constexpr uint32_t INPUT_TOKEN_RELEASED_SHIFT = INPUT_TOKEN_PRESSED_SHIFT + 1;
constexpr uint32_t INPUT_TOKEN_KIND_MASK = (1u << INPUT_TOKEN_KIND_BITS) - 1u;
constexpr uint32_t INPUT_TOKEN_DEVICE_MASK = (1u << INPUT_TOKEN_DEVICE_BITS) - 1u;
constexpr uint32_t INPUT_TOKEN_AXIS_CODE_MASK = (1u << INPUT_TOKEN_AXIS_CODE_BITS) - 1u;
constexpr uint32_t INPUT_TOKEN_AXIS_VALUE_MASK = (1u << INPUT_TOKEN_AXIS_VALUE_BITS) - 1u;
constexpr uint64_t INPUT_TOKEN_DIGITAL_CODE_MASK = (uint64_t(1) << 32) - 1;

inline int64_t pack_input_event_token(int32_t kind, int32_t device, int64_t code,
    float value, bool pressed, bool released) {
    uint32_t payload = uint32_t(code);
    if (kind == ELISA_APPLICATION_INPUT_GAMEPAD_AXIS) {
        const uint32_t portable_code = uint32_t(code) & INPUT_TOKEN_AXIS_CODE_MASK;
        const float bounded_value = value < 0.0f ? 0.0f : value > 1.0f ? 1.0f : value;
        const uint32_t quantized_value = uint32_t(
            bounded_value * float(INPUT_TOKEN_AXIS_VALUE_MASK) + 0.5f);
        payload = (portable_code << INPUT_TOKEN_AXIS_VALUE_BITS) | quantized_value;
    }
    const uint64_t packed = uint64_t(kind & INPUT_TOKEN_KIND_MASK) |
        (uint64_t(device & INPUT_TOKEN_DEVICE_MASK) << INPUT_TOKEN_DEVICE_SHIFT) |
        (uint64_t(payload) << INPUT_TOKEN_PAYLOAD_SHIFT) |
        (pressed ? uint64_t(1) << INPUT_TOKEN_PRESSED_SHIFT : 0) |
        (released ? uint64_t(1) << INPUT_TOKEN_RELEASED_SHIFT : 0);
    return int64_t(packed);
}

// Stable ActionInput codes. They are deliberately independent of the SDL
// enum's numeric values so Elisa projects never need to import SDL headers.
enum ElisaGamepadCode : int32_t {
    GAMEPAD_BUTTON_SOUTH = 1001,
    GAMEPAD_BUTTON_EAST = 1002,
    GAMEPAD_BUTTON_WEST = 1003,
    GAMEPAD_BUTTON_NORTH = 1004,
    GAMEPAD_BUTTON_BACK = 1005,
    GAMEPAD_BUTTON_GUIDE = 1006,
    GAMEPAD_BUTTON_START = 1007,
    GAMEPAD_BUTTON_LEFT_STICK = 1008,
    GAMEPAD_BUTTON_RIGHT_STICK = 1009,
    GAMEPAD_BUTTON_LEFT_SHOULDER = 1010,
    GAMEPAD_BUTTON_RIGHT_SHOULDER = 1011,
    GAMEPAD_BUTTON_DPAD_UP = 1012,
    GAMEPAD_BUTTON_DPAD_DOWN = 1013,
    GAMEPAD_BUTTON_DPAD_LEFT = 1014,
    GAMEPAD_BUTTON_DPAD_RIGHT = 1015,

    GAMEPAD_AXIS_LEFT_X_NEGATIVE = 1101,
    GAMEPAD_AXIS_LEFT_X_POSITIVE = 1102,
    GAMEPAD_AXIS_LEFT_Y_NEGATIVE = 1103,
    GAMEPAD_AXIS_LEFT_Y_POSITIVE = 1104,
    GAMEPAD_AXIS_RIGHT_X_NEGATIVE = 1105,
    GAMEPAD_AXIS_RIGHT_X_POSITIVE = 1106,
    GAMEPAD_AXIS_RIGHT_Y_NEGATIVE = 1107,
    GAMEPAD_AXIS_RIGHT_Y_POSITIVE = 1108,
    GAMEPAD_AXIS_LEFT_TRIGGER = 1109,
    GAMEPAD_AXIS_RIGHT_TRIGGER = 1110,
};

enum ElisaKeyboardCode : int32_t {
    KEY_A = 2001,
    KEY_D = 2002,
    KEY_E = 2003,
    KEY_Q = 2004,
    KEY_R = 2005,
    KEY_S = 2006,
    KEY_W = 2007,
    KEY_P = 2008,
    KEY_SPACE = 2009,
    KEY_ESCAPE = 2010,
    KEY_LEFT_SHIFT = 2011,
    KEY_RIGHT_SHIFT = 2012,
    KEY_ARROW_LEFT = 2013,
    KEY_ARROW_RIGHT = 2014,
    KEY_ARROW_UP = 2015,
    KEY_ARROW_DOWN = 2016,
};

constexpr int32_t keyboard_key_code(SDL_Keycode key) {
    switch (key) {
    case SDLK_A: return KEY_A;
    case SDLK_D: return KEY_D;
    case SDLK_E: return KEY_E;
    case SDLK_Q: return KEY_Q;
    case SDLK_R: return KEY_R;
    case SDLK_S: return KEY_S;
    case SDLK_W: return KEY_W;
    case SDLK_P: return KEY_P;
    case SDLK_SPACE: return KEY_SPACE;
    case SDLK_ESCAPE: return KEY_ESCAPE;
    case SDLK_LSHIFT: return KEY_LEFT_SHIFT;
    case SDLK_RSHIFT: return KEY_RIGHT_SHIFT;
    case SDLK_LEFT: return KEY_ARROW_LEFT;
    case SDLK_RIGHT: return KEY_ARROW_RIGHT;
    case SDLK_UP: return KEY_ARROW_UP;
    case SDLK_DOWN: return KEY_ARROW_DOWN;
    default: return 0;
    }
}

constexpr int32_t gamepad_button_code(SDL_GamepadButton button) {
    switch (button) {
    case SDL_GAMEPAD_BUTTON_SOUTH: return GAMEPAD_BUTTON_SOUTH;
    case SDL_GAMEPAD_BUTTON_EAST: return GAMEPAD_BUTTON_EAST;
    case SDL_GAMEPAD_BUTTON_WEST: return GAMEPAD_BUTTON_WEST;
    case SDL_GAMEPAD_BUTTON_NORTH: return GAMEPAD_BUTTON_NORTH;
    case SDL_GAMEPAD_BUTTON_BACK: return GAMEPAD_BUTTON_BACK;
    case SDL_GAMEPAD_BUTTON_GUIDE: return GAMEPAD_BUTTON_GUIDE;
    case SDL_GAMEPAD_BUTTON_START: return GAMEPAD_BUTTON_START;
    case SDL_GAMEPAD_BUTTON_LEFT_STICK: return GAMEPAD_BUTTON_LEFT_STICK;
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return GAMEPAD_BUTTON_RIGHT_STICK;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return GAMEPAD_BUTTON_LEFT_SHOULDER;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return GAMEPAD_BUTTON_RIGHT_SHOULDER;
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return GAMEPAD_BUTTON_DPAD_UP;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return GAMEPAD_BUTTON_DPAD_DOWN;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return GAMEPAD_BUTTON_DPAD_LEFT;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return GAMEPAD_BUTTON_DPAD_RIGHT;
    default: return 0;
    }
}

struct GamepadAxisInput {
    int32_t negative_code = 0;
    int32_t positive_code = 0;
    float negative_value = 0.0f;
    float positive_value = 0.0f;
};

inline GamepadAxisInput gamepad_axis_input(SDL_GamepadAxis axis, int16_t raw_value) {
    GamepadAxisInput result;
    const float signed_value = raw_value < 0
        ? static_cast<float>(raw_value) / 32768.0f
        : static_cast<float>(raw_value) / 32767.0f;
    switch (axis) {
    case SDL_GAMEPAD_AXIS_LEFTX:
        result.negative_code = GAMEPAD_AXIS_LEFT_X_NEGATIVE;
        result.positive_code = GAMEPAD_AXIS_LEFT_X_POSITIVE;
        break;
    case SDL_GAMEPAD_AXIS_LEFTY:
        result.negative_code = GAMEPAD_AXIS_LEFT_Y_NEGATIVE;
        result.positive_code = GAMEPAD_AXIS_LEFT_Y_POSITIVE;
        break;
    case SDL_GAMEPAD_AXIS_RIGHTX:
        result.negative_code = GAMEPAD_AXIS_RIGHT_X_NEGATIVE;
        result.positive_code = GAMEPAD_AXIS_RIGHT_X_POSITIVE;
        break;
    case SDL_GAMEPAD_AXIS_RIGHTY:
        result.negative_code = GAMEPAD_AXIS_RIGHT_Y_NEGATIVE;
        result.positive_code = GAMEPAD_AXIS_RIGHT_Y_POSITIVE;
        break;
    case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:
        result.positive_code = GAMEPAD_AXIS_LEFT_TRIGGER;
        result.positive_value = signed_value > 0.0f ? signed_value : 0.0f;
        return result;
    case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER:
        result.positive_code = GAMEPAD_AXIS_RIGHT_TRIGGER;
        result.positive_value = signed_value > 0.0f ? signed_value : 0.0f;
        return result;
    default:
        return result;
    }
    result.negative_value = signed_value < 0.0f ? -signed_value : 0.0f;
    result.positive_value = signed_value > 0.0f ? signed_value : 0.0f;
    return result;
}

} // namespace probe
