#include "../native/character_jump_policy.h"
#include <cstdio>
#include <initializer_list>

int main() {
    constexpr float velocities[] = {-100.0f, -6.231466f, -0.1635f, 0.0f, 3.0f, 10.0f};
    constexpr float supports[] = {-2.0f, 0.0f, 2.0f};
    constexpr float speeds[] = {-1.0f, 0.0f, 0.1f, 5.0f};
    unsigned cases = 0;
    for (float velocity : velocities) for (float support : supports)
        for (float speed : speeds) for (bool grounded : {false, true}) {
            const float result = elisa_character::jump_vertical_velocity(velocity, support, speed, grounded);
            const float minimum = support + speed;
            const float expected = grounded && speed > 0.0f && velocity < minimum ? minimum : velocity;
            if (result != expected) return 1;
            ++cases;
        }
    // Observed course failure: backend movement left the jump pointing down.
    if (elisa_character::jump_vertical_velocity(-1.231466f, 0.0f, 5.0f, true) != 5.0f) return 2;
    std::printf("Grounded jump policy passed %u finite cases and the course regression.\n", cases);
    return 0;
}
