#pragma once

#include "physics_query_bridge.h"

namespace probe {

inline bool probe_physics_queries(wi::scene::Scene& scene) {
    const size_t before = scene.objects.GetCount();
    const auto target = scene.Entity_CreateCube("elisa_query_target");
    const auto secondary = scene.Entity_CreateCube("elisa_query_secondary");
    if (!check(target != wi::ecs::INVALID_ENTITY && secondary != wi::ecs::INVALID_ENTITY,
            "query target entities")) return false;
    auto* transform = scene.transforms.GetComponent(target);
    auto* layer = scene.layers.GetComponent(target);
    auto* secondary_transform = scene.transforms.GetComponent(secondary);
    auto* secondary_layer = scene.layers.GetComponent(secondary);
    if (!check(transform != nullptr && layer != nullptr && secondary_transform != nullptr &&
            secondary_layer != nullptr, "query target components")) return false;
    transform->translation_local = XMFLOAT3(0, 0, 0);
    transform->UpdateTransform();
    layer->layerMask = 1u << 3;
    secondary_transform->translation_local = XMFLOAT3(0, 0, 3);
    secondary_transform->UpdateTransform();
    secondary_layer->layerMask = 1u << 3;
    scene.Update(0.0f);

    PhysicsQueryBridge bridge(scene);
    const auto token = bridge.acquire();
    PhysicsQueryBridge foreign_bridge(scene);
    const auto foreign_token = foreign_bridge.acquire();
    PhysicsQueryHit hit;
    PhysicsQueryBridge::Hits hits;
    if (!check(!bridge.raycast(foreign_token, XMFLOAT3(0, 0, -4), XMFLOAT3(0, 0, 1), 10.0f,
            1u << 3, hit), "query rejects foreign owner") ||
        !check(bridge.raycast(token, XMFLOAT3(0, 0, -4), XMFLOAT3(0, 0, 1), 10.0f,
            1u << 3, hit) && hit.entity == target && hit.distance > 0.0f,
            "query raycast hit") ||
        !check(bridge.sphere_cast(token, XMFLOAT3(0, 0, -5), XMFLOAT3(0, 0, 1),
            10.0f, 0.25f, 1u << 3, hit) && hit.entity == target && hit.distance > 0.0f,
            "query sphere cast hit") ||
        !check(bridge.capsule_cast(token, XMFLOAT3(0, 0, -5), XMFLOAT3(0, 0, -4),
            XMFLOAT3(0, 0, 1), 10.0f, 0.25f, 1u << 3, hit) && hit.entity == target,
            "query capsule cast hit") ||
        !check(!bridge.raycast(token, XMFLOAT3(0, 0, -4), XMFLOAT3(0, 0, 1), 10.0f,
            1u << 1, hit), "query layer filter") ||
        !check(!bridge.sphere_cast(token, XMFLOAT3(0, 0, -5), XMFLOAT3(0, 0, 1),
            10.0f, 0.25f, 1u << 1, hit), "query cast layer filter") ||
        !check(!bridge.sphere_cast(token, XMFLOAT3(0, 0, -5), XMFLOAT3(0, 0, 0),
            10.0f, 0.25f, 1u << 3, hit), "query rejects zero cast direction") ||
        !check(bridge.overlap_sphere(token, XMFLOAT3(0, 0, -1.5f), 1.0f, 1u << 3, hit) &&
            hit.entity == target && hit.depth >= 0.0f, "query sphere overlap") ||
        !check(bridge.overlap_sphere_all(token, XMFLOAT3(0, 0, 1.5f), 2.0f, 1u << 3, hits) == 2 &&
            contains_entity(hits, target) && contains_entity(hits, secondary),
            "query sphere all overlaps") ||
        !check(bridge.overlap_capsule(token, XMFLOAT3(0, 0, -2), XMFLOAT3(0, 0, 2),
            1.0f, 1u << 3, hit) && hit.entity == target && hit.depth >= 0.0f,
            "query capsule overlap") ||
        !check(bridge.overlap_capsule_all(token, XMFLOAT3(0, 0, 1.4f), XMFLOAT3(0, 0, 1.6f),
            3.0f, 1u << 3, hits) == 2 && contains_entity(hits, target) &&
            contains_entity(hits, secondary), "query capsule all overlaps")) return false;

    if (!check(bridge.raycast_all(token, XMFLOAT3(0, 0, -4), XMFLOAT3(0, 0, 1),
            10.0f, 1u << 3, hits) > 0 && hits.count <= PhysicsQueryBridge::MAX_HITS,
            "query bounded all hits")) return false;
    const size_t scene_object_hits = bridge.raycast_all(token,
        XMFLOAT3(0, 0, -4), XMFLOAT3(0, 0, 1), 10.0f, 1u << 3, hits,
        wi::enums::FILTER_OBJECT_ALL, false);
    if (!check(scene_object_hits >= 2 && hits.count <= PhysicsQueryBridge::MAX_HITS &&
            contains_entity(hits, target) && contains_entity(hits, secondary),
            "query scene-object filter") ||
        !check(bridge.raycast_all(token, XMFLOAT3(0, 0, -4), XMFLOAT3(0, 0, 1),
            10.0f, 1u << 3, hits, wi::enums::FILTER_NONE, false) == 0 && hits.count == 0,
            "query empty filter")) return false;
    const auto collider_entity = scene.Entity_CreateTransform("elisa_query_collider");
    auto* collider_transform = scene.transforms.GetComponent(collider_entity);
    if (!check(collider_entity != wi::ecs::INVALID_ENTITY && collider_transform != nullptr,
            "query collider entity")) return false;
    collider_transform->translation_local = XMFLOAT3(0, 0, 6);
    collider_transform->UpdateTransform();
    auto& collider = scene.colliders.Create(collider_entity);
    collider.shape = wi::scene::ColliderComponent::Shape::Sphere;
    collider.radius = 0.5f;
    collider.layerMask = 1u << 3;
    scene.Update(0.0f);
    scene.Update(0.0f);
    const size_t collider_hits = bridge.raycast_all(token, XMFLOAT3(0, 0, -4),
        XMFLOAT3(0, 0, 1), 10.0f, 1u << 3, hits,
        wi::enums::FILTER_COLLIDER, false);
    if (!check(collider_hits >= 1 && contains_entity(hits, collider_entity),
            "query scene-collider filter")) return false;
    scene.Entity_Remove(collider_entity);
    scene.Entity_Remove(target);
    scene.Entity_Remove(secondary);
    scene.Update(0.0f);
    if (!check(!bridge.raycast(token, XMFLOAT3(0, 0, -4), XMFLOAT3(0, 0, 1), 10.0f,
            1u << 3, hit), "query rejects destroyed participant")) return false;
    bridge.invalidate();
    if (!check(!bridge.raycast(token, XMFLOAT3(0, 0, -4), XMFLOAT3(0, 0, 1), 10.0f,
            1u << 3, hit), "query rejects stale token")) return false;
    return check(scene.objects.GetCount() == before, "query unloads target");
}

} // namespace probe
