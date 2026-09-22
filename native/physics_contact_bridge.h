#pragma once

// Real Jolt contact delivery through the bounded owner-thread queue. The
// probe creates two ordinary Wicked rigid bodies so this covers registration,
// worker callback handoff, entity identity, and teardown ordering.
#include "physics_body_bridge.h"
#include "physics_query_bridge.h"

namespace probe {

inline bool probe_physics_contact_listener(wi::scene::Scene& scene) {
    PhysicsBodyBridge bridge(scene);
    const PhysicsBodyHandle floor = bridge.create(PhysicsBodyKind::Static, 0.0f, 1);
    const PhysicsBodyHandle body = bridge.create(PhysicsBodyKind::Dynamic, 1.0f, 2);
    if (!check(bridge.live(floor) && bridge.live(body), "contact bridge creates bodies")) return false;
    const auto floor_entity = bridge.resolve(floor);
    const auto body_entity = bridge.resolve(body);
    auto* floor_transform = scene.transforms.GetComponent(floor_entity);
    auto* body_transform = scene.transforms.GetComponent(body_entity);
    if (!check(floor_transform != nullptr && body_transform != nullptr,
            "contact bridge resolves transforms")) return false;
    floor_transform->translation_local = XMFLOAT3(0, 0, 0);
    body_transform->translation_local = XMFLOAT3(0, 0.25f, 0);
    floor_transform->UpdateTransform();
    body_transform->UpdateTransform();
    scene.Update(0.0f);

    PhysicsContactQueue queue;
    PhysicsContactQueueListener listener(queue);
    wi::physics::SetContactEventListener(scene, &listener);
    if (!check(queue.begin_step(1), "contact bridge begins fixed step")) return false;
    bool stepped = true;
    for (size_t step = 0; step < 4 && queue.event_count() == 0; ++step) {
        stepped = bridge.step_fixed(1.0f / 120.0f) && stepped;
    }
    if (!check(stepped, "contact bridge advances fixed step")) return false;

    bool saw_pair = false;
    bool saw_trigger = false;
    const size_t delivered = queue.drain([&](const PhysicsContactEvent& event) {
        if ((event.entity_a == floor_entity && event.entity_b == body_entity) ||
            (event.entity_a == body_entity && event.entity_b == floor_entity)) {
            saw_pair = true;
            saw_trigger = event.trigger;
        }
    });
    if (!check(delivered > 0 && saw_pair && !saw_trigger,
            "contact bridge delivers real Jolt contact")) return false;
    if (!check(queue.end_step(), "contact bridge closes fixed step")) return false;
    if (!check(queue.begin_step(2) && bridge.destroy(body),
            "contact bridge removes participant")) return false;
    if (!check(bridge.step_fixed(1.0f / 60.0f), "contact bridge advances removal step")) return false;
    bool saw_removed = false;
    queue.drain([&](const PhysicsContactEvent& event) {
        saw_removed = saw_removed || (event.kind == PhysicsContactKind::Removed &&
            ((event.entity_a == floor_entity && event.entity_b == body_entity) ||
             (event.entity_a == body_entity && event.entity_b == floor_entity)));
    });
    if (!check(saw_removed, "contact bridge delivers Jolt removal")) return false;
    if (!check(queue.end_step(), "contact bridge closes removal step")) return false;
    wi::physics::SetContactEventListener(scene, nullptr);
    if (!check(bridge.destroy(floor), "contact bridge destroys remaining participant")) return false;
    scene.Update(0.0f);
    return check(bridge.live_count() == 0, "contact bridge unloads participants");
}

} // namespace probe
