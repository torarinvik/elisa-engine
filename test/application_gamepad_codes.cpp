#include "../native/application_input_codes.h"

#include <cmath>
#include <cstdio>

int main() {
    using namespace probe;
    if (keyboard_key_code(SDLK_A) != KEY_A || keyboard_key_code(SDLK_LEFT) != KEY_ARROW_LEFT ||
        keyboard_key_code(SDLK_F1) != 0) {
        std::fprintf(stderr, "portable keyboard mapping failed\n");
        return 7;
    }
    if (gamepad_button_code(SDL_GAMEPAD_BUTTON_SOUTH) != GAMEPAD_BUTTON_SOUTH ||
        gamepad_button_code(SDL_GAMEPAD_BUTTON_DPAD_RIGHT) != GAMEPAD_BUTTON_DPAD_RIGHT ||
        gamepad_button_code(SDL_GAMEPAD_BUTTON_MISC1) != 0) {
        std::fprintf(stderr, "portable gamepad button mapping failed\n");
        return 1;
    }
    const GamepadAxisInput left = gamepad_axis_input(SDL_GAMEPAD_AXIS_LEFTX, -16384);
    if (left.negative_code != GAMEPAD_AXIS_LEFT_X_NEGATIVE ||
        left.positive_code != GAMEPAD_AXIS_LEFT_X_POSITIVE ||
        std::fabs(left.negative_value - 0.5f) > 0.001f || left.positive_value != 0.0f) {
        std::fprintf(stderr, "negative stick normalization failed\n");
        return 2;
    }
    const GamepadAxisInput right = gamepad_axis_input(SDL_GAMEPAD_AXIS_LEFTX, 16384);
    if (std::fabs(right.positive_value - (16384.0f / 32767.0f)) > 0.001f ||
        right.negative_value != 0.0f) {
        std::fprintf(stderr, "positive stick normalization failed\n");
        return 3;
    }
    const GamepadAxisInput trigger = gamepad_axis_input(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 32767);
    if (trigger.positive_code != GAMEPAD_AXIS_RIGHT_TRIGGER || trigger.positive_value != 1.0f ||
        trigger.negative_code != 0) {
        std::fprintf(stderr, "trigger normalization failed\n");
        return 4;
    }
    const int64_t axis_token = pack_input_event_token(
        ELISA_APPLICATION_INPUT_GAMEPAD_AXIS, INPUT_DEVICE_GAMEPAD,
        GAMEPAD_AXIS_LEFT_X_POSITIVE, 0.5f, true, false);
    const uint64_t encoded_axis = uint64_t(axis_token);
    const int32_t axis_code = static_cast<int32_t>(
        (encoded_axis >> INPUT_TOKEN_AXIS_CODE_SHIFT) & INPUT_TOKEN_AXIS_CODE_MASK);
    const float axis_value = static_cast<float>(
        (encoded_axis >> INPUT_TOKEN_PAYLOAD_SHIFT) & INPUT_TOKEN_AXIS_VALUE_MASK) /
        float(INPUT_TOKEN_AXIS_VALUE_MASK);
    if ((encoded_axis & INPUT_TOKEN_KIND_MASK) != ELISA_APPLICATION_INPUT_GAMEPAD_AXIS ||
        ((encoded_axis >> INPUT_TOKEN_DEVICE_SHIFT) & INPUT_TOKEN_DEVICE_MASK) != INPUT_DEVICE_GAMEPAD ||
        axis_code != GAMEPAD_AXIS_LEFT_X_POSITIVE ||
        std::fabs(axis_value - 0.5f) > 0.00001f ||
        ((encoded_axis >> INPUT_TOKEN_PRESSED_SHIFT) & 1) == 0) {
        std::fprintf(stderr, "analog input token encoding failed\n");
        return 5;
    }
    const int64_t key_token = pack_input_event_token(
        ELISA_APPLICATION_INPUT_KEY, INPUT_DEVICE_KEYBOARD, KEY_ARROW_LEFT, 1.0f, true, false);
    if (((uint64_t(key_token) >> INPUT_TOKEN_PAYLOAD_SHIFT) & INPUT_TOKEN_DIGITAL_CODE_MASK) != uint32_t(KEY_ARROW_LEFT)) {
        std::fprintf(stderr, "digital input token compatibility failed\n");
        return 6;
    }
    return 0;
}
