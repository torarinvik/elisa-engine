#pragma once

namespace elisa_character {

// The support query can report OnGround before the contact solver has removed
// fall velocity. A requested jump must not be consumed by that residual fall.
// Inputs and the eligible sum are finite; the native adapter checks them.
constexpr float jump_vertical_velocity(float resulting_velocity,
    float support_velocity, float requested_speed, bool grounded) noexcept {
    if (!grounded || requested_speed <= 0.0f) return resulting_velocity;
#if defined(ELISA_TEST_DISABLE_GROUNDED_JUMP)
    return resulting_velocity;
#else
    const float minimum = support_velocity + requested_speed;
    return resulting_velocity < minimum ? minimum : resulting_velocity;
#endif
}

} // namespace elisa_character
