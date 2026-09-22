#pragma once

// Generation-checked Elisa-facing body handles backed by Wicked rigid-body
// components. Jolt entity IDs stay inside this adapter and are never exposed.
#include "probe_core.h"
#include "wiScene.h"
#include "wiPhysics.h"
#include "coordinate_transform_bridge.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <string>

namespace probe {

enum class PhysicsBodyKind : uint8_t { Static, Kinematic, Dynamic };

struct PhysicsBodyHandle {
    uint32_t slot = UINT32_MAX;
    uint32_t generation = 0;
    uintptr_t owner = 0;
};

class PhysicsBodyBridge {
public:
    static constexpr uint32_t MAX_BODIES = 64;
    explicit PhysicsBodyBridge(wi::scene::Scene& scene)
        : scene_(scene), owner_(reinterpret_cast<uintptr_t>(&scene)) {}
    PhysicsBodyBridge(const PhysicsBodyBridge&) = delete;
    PhysicsBodyBridge& operator=(const PhysicsBodyBridge&) = delete;

    PhysicsBodyHandle create(PhysicsBodyKind kind, float mass, int layer) {
        if (mass < 0.0f || (kind == PhysicsBodyKind::Dynamic && mass <= 0.0f) || layer < 0) return {};
        const uint32_t slot = free_slot();
        if (slot == MAX_BODIES) return {};
        const auto entity = scene_.Entity_CreateCube("elisa_body_" + std::to_string(slot));
        if (entity == wi::ecs::INVALID_ENTITY) return {};
        auto& body = scene_.rigidbodies.Create(entity);
        body.shape = wi::scene::RigidBodyPhysicsComponent::BOX;
        body.mass = kind == PhysicsBodyKind::Static ? 0.0f : mass;
        body.box.halfextents = XMFLOAT3(0.5f, 0.5f, 0.5f);
        body._flags = kind == PhysicsBodyKind::Kinematic
            ? wi::scene::RigidBodyPhysicsComponent::KINEMATIC : 0;
        Slot& state = slots_[slot];
        if (state.generation == UINT32_MAX) {
            scene_.Entity_Remove(entity);
            return {};
        }
        state.generation = state.generation == 0 ? 1 : state.generation + 1;
        state.entity = entity;
        state.kind = kind;
        state.layer = layer;
        state.live = true;
        return PhysicsBodyHandle{slot, state.generation, owner_};
    }

    bool live(PhysicsBodyHandle handle) const {
        return handle.owner == owner_ && handle.slot < MAX_BODIES && slots_[handle.slot].live &&
            slots_[handle.slot].generation == handle.generation;
    }

    wi::ecs::Entity resolve(PhysicsBodyHandle handle) const {
        return live(handle) ? slots_[handle.slot].entity : wi::ecs::INVALID_ENTITY;
    }

    bool set_kinematic_target(PhysicsBodyHandle handle, const XMFLOAT3& position,
        const XMFLOAT4& rotation) {
        if (!live(handle) || slots_[handle.slot].kind != PhysicsBodyKind::Kinematic) return false;
        auto* transform = scene_.transforms.GetComponent(slots_[handle.slot].entity);
        if (transform == nullptr) return false;
        // Wicked feeds kinematic targets from the scene transform during its
        // pre-simulation pass. Publishing only this owner-side transform
        // avoids teleporting the body and lets Jolt compute target velocity.
        transform->translation_local = position;
        transform->rotation_local = rotation;
        transform->SetDirty();
        return true;
    }

    bool set_sleeping(PhysicsBodyHandle handle, bool sleeping) {
        if (!live(handle) || slots_[handle.slot].kind != PhysicsBodyKind::Dynamic) return false;
        auto* body = scene_.rigidbodies.GetComponent(slots_[handle.slot].entity);
        if (body == nullptr) return false;
        wi::physics::SetActivationState(*body, sleeping
            ? wi::physics::ActivationState::Inactive
            : wi::physics::ActivationState::Active);
        return true;
    }

    bool destroy(PhysicsBodyHandle handle) {
        if (!live(handle)) return false;
        scene_.Entity_Remove(slots_[handle.slot].entity);
        slots_[handle.slot].entity = wi::ecs::INVALID_ENTITY;
        slots_[handle.slot].live = false;
        return true;
    }

    uint32_t live_count() const {
        uint32_t result = 0;
        for (const Slot& slot : slots_) result += slot.live ? 1 : 0;
        return result;
    }

    bool step_fixed(float dt) {
        if (!std::isfinite(dt) || dt <= 0.0f || dt > 1.0f / 30.0f) return false;
        scene_.Update(dt);
        return true;
    }

private:
    struct Slot {
        wi::ecs::Entity entity = wi::ecs::INVALID_ENTITY;
        uint32_t generation = 0;
        int layer = 0;
        PhysicsBodyKind kind = PhysicsBodyKind::Static;
        bool live = false;
    };

    uint32_t free_slot() const {
        for (uint32_t index = 0; index < MAX_BODIES; ++index) {
            if (!slots_[index].live) return index;
        }
        return MAX_BODIES;
    }

    wi::scene::Scene& scene_;
    uintptr_t owner_;
    std::array<Slot, MAX_BODIES> slots_{};
};

inline bool probe_physics_body_bridge(wi::scene::Scene& scene) {
    const size_t before = scene.rigidbodies.GetCount();
    PhysicsBodyBridge bridge(scene);
    const PhysicsBodyHandle static_body = bridge.create(PhysicsBodyKind::Static, 0.0f, 1);
    const PhysicsBodyHandle kinematic = bridge.create(PhysicsBodyKind::Kinematic, 1.0f, 2);
    const PhysicsBodyHandle dynamic = bridge.create(PhysicsBodyKind::Dynamic, 2.0f, 3);
    if (!check(bridge.live(static_body) && bridge.live(kinematic) && bridge.live(dynamic),
        "physics bridge creates typed bodies") ||
        !check(bridge.live_count() == 3 && scene.rigidbodies.GetCount() == before + 3,
            "physics bridge owns body count") ||
        !check(!bridge.live(PhysicsBodyHandle{dynamic.slot, dynamic.generation, 0}),
            "physics bridge rejects foreign owner")) return false;
    if (!check(bridge.destroy(kinematic), "physics bridge destroys body") ||
        !check(!bridge.live(kinematic) && !bridge.destroy(kinematic), "physics bridge rejects stale body") ||
        !check(bridge.destroy(static_body) && bridge.destroy(dynamic) && bridge.live_count() == 0 &&
            scene.rigidbodies.GetCount() == before, "physics bridge unloads bodies")) return false;
    wi::physics::SetSimulationEnabled(true);
    wi::physics::SetInterpolationEnabled(false);
    const PhysicsBodyHandle stepped = bridge.create(PhysicsBodyKind::Dynamic, 1.0f, 4);
    auto* stepped_transform = scene.transforms.GetComponent(bridge.resolve(stepped));
    if (!check(stepped_transform != nullptr, "physics bridge step transform")) return false;
    const ElisaCoordinateProfile profile = elisa_coordinate_profile();
    const ElisaTransformPayload authored{{0.0f, 4.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f},
        {-0.5f, 0.25f, 1.5f}};
    if (!check(submit_elisa_transform(&profile, &authored, stepped_transform),
        "physics body submits signed nonuniform Elisa transform")) return false;
    stepped_transform->UpdateTransform();
    constexpr float FIXED_DT = 1.0f / 120.0f;
    const float start_y = stepped_transform->GetPosition().y;
    if (!check(bridge.step_fixed(FIXED_DT), "physics bridge fixed step one") ||
        !check(bridge.step_fixed(FIXED_DT), "physics bridge fixed step two")) return false;
    const float after_two_steps = stepped_transform->GetPosition().y;
    const wi::physics::RayIntersectionResult hit = wi::physics::Intersects(scene,
        wi::primitive::Ray(XMFLOAT3(0.0f, start_y, -4.0f), XMFLOAT3(0.0f, 0.0f, 1.0f), 0.0f, 10.0f));
    const wi::physics::RayIntersectionResult miss = wi::physics::Intersects(scene,
        wi::primitive::Ray(XMFLOAT3(0.0f, start_y + 0.3f, -4.0f), XMFLOAT3(0.0f, 0.0f, 1.0f), 0.0f, 10.0f));
    if (!check(after_two_steps < start_y, "physics bridge advances fixed body once per tick") ||
        !check(hit.entity == bridge.resolve(stepped), "Jolt ray hits signed-scale physics shape") ||
        !check(miss.entity != bridge.resolve(stepped), "Jolt ray respects nonuniform physics extent")) return false;
    if (!check(bridge.destroy(stepped), "physics bridge fixed-step unload")) return false;

    const PhysicsBodyHandle target = bridge.create(PhysicsBodyKind::Kinematic, 1.0f, 5);
    if (!check(target.slot != UINT32_MAX, "physics bridge creates kinematic target") ||
        !check(bridge.step_fixed(FIXED_DT), "physics bridge initializes kinematic target")) return false;
    const XMFLOAT4 identity_rotation(0, 0, 0, 1);
    if (!check(bridge.set_kinematic_target(target, XMFLOAT3(3.0f, 2.0f, 0.0f), identity_rotation),
        "physics bridge accepts kinematic target") ||
        !check(!bridge.set_kinematic_target(dynamic, XMFLOAT3(3.0f, 2.0f, 0.0f), identity_rotation),
            "physics bridge rejects dynamic kinematic target") ||
        !check(bridge.step_fixed(FIXED_DT), "physics bridge commits kinematic target")) return false;
    auto* target_transform = scene.transforms.GetComponent(bridge.resolve(target));
    if (!check(target_transform != nullptr &&
        std::fabs(target_transform->GetPosition().x - 3.0f) < 0.01f &&
        std::fabs(target_transform->GetPosition().y - 2.0f) < 0.01f,
        "physics bridge publishes kinematic target pose")) return false;

    const PhysicsBodyHandle sleeping = bridge.create(PhysicsBodyKind::Dynamic, 1.0f, 6);
    if (!check(sleeping.slot != UINT32_MAX, "physics bridge creates sleepable body") ||
        !check(bridge.step_fixed(FIXED_DT), "physics bridge initializes sleepable body")) return false;
    auto* sleeping_transform = scene.transforms.GetComponent(bridge.resolve(sleeping));
    if (!check(sleeping_transform != nullptr, "physics bridge sleep transform")) return false;
    const float sleeping_y = sleeping_transform->GetPosition().y;
    if (!check(bridge.set_sleeping(sleeping, true), "physics bridge deactivates dynamic body") ||
        !check(!bridge.set_sleeping(target, true), "physics bridge rejects sleeping kinematic body") ||
        !check(bridge.step_fixed(FIXED_DT), "physics bridge advances sleeping body")) return false;
    const float held_y = sleeping_transform->GetPosition().y;
    if (!check(std::fabs(held_y - sleeping_y) < 0.001f, "physics bridge keeps sleeping pose") ||
        !check(bridge.set_sleeping(sleeping, false), "physics bridge wakes dynamic body") ||
        !check(bridge.step_fixed(FIXED_DT), "physics bridge advances woken body") ||
        !check(bridge.step_fixed(FIXED_DT), "physics bridge advances woken body twice") ||
        !check(sleeping_transform->GetPosition().y < held_y, "physics bridge wakes into simulation")) {
        std::fprintf(stdout, "sleeping body poses: %.4f -> %.4f -> %.4f\n",
            sleeping_y, held_y, sleeping_transform->GetPosition().y);
        return false;
    }
    if (!check(bridge.destroy(target) && bridge.destroy(sleeping), "physics bridge unloads motion bodies")) return false;
    return true;
}

} // namespace probe
