#pragma once

#include "probe_support.h"
#include "wiApplication.h"
#include "wiPhysics.h"
#include "wiHelper.h"

#include <cmath>

namespace probe {

inline bool probe_physics_pause(wi::Application& application, wi::scene::TransformComponent& transform) {
    const float before = transform.GetPosition().y;
    wi::physics::SetSimulationEnabled(false);
    for (int frame = 0; frame < 2; ++frame) {
        application.Run();
        wi::helper::Sleep(10);
    }
    const float paused = transform.GetPosition().y;
    wi::physics::SetSimulationEnabled(true);
    return check(std::abs(paused - before) < 0.001f, "physics pause owns simulation") &&
        check(wi::physics::IsSimulationEnabled(), "physics simulation resumes");
}

} // namespace probe
