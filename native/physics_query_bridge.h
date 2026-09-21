#pragma once

// Bounded scene collision queries for the Elisa/native boundary. Wicked keeps
// its entity and intersection types private to this adapter; callers receive
// only copied hit data and a generation-checked query token.
#include "probe_core.h"
#include "wiScene.h"

#include <array>
#include <cmath>
#include <cstdint>

namespace probe {

struct PhysicsQueryToken {
    uintptr_t owner = 0;
    uint32_t generation = 0;
};

struct PhysicsQueryHit {
    wi::ecs::Entity entity = wi::ecs::INVALID_ENTITY;
    XMFLOAT3 position = XMFLOAT3(0, 0, 0);
    XMFLOAT3 normal = XMFLOAT3(0, 0, 0);
    float distance = 0.0f;
    float depth = 0.0f;
};

template <size_t Capacity>
struct PhysicsQueryHits {
    std::array<PhysicsQueryHit, Capacity> values{};
    size_t count = 0;
};

template <size_t Capacity>
static bool contains_entity(const PhysicsQueryHits<Capacity>& hits, wi::ecs::Entity entity) {
    for (size_t index = 0; index < hits.count; ++index) {
        if (hits.values[index].entity == entity) return true;
    }
    return false;
}

class PhysicsQueryBridge {
public:
    static constexpr size_t MAX_HITS = 16;
    using Hits = PhysicsQueryHits<MAX_HITS>;

    explicit PhysicsQueryBridge(wi::scene::Scene& scene)
        : scene_(scene), owner_(reinterpret_cast<uintptr_t>(this)) {}
    PhysicsQueryBridge(const PhysicsQueryBridge&) = delete;
    PhysicsQueryBridge& operator=(const PhysicsQueryBridge&) = delete;

    PhysicsQueryToken acquire() const { return {owner_, generation_}; }

    void invalidate() {
        if (generation_ != UINT32_MAX) ++generation_;
    }

    bool valid(PhysicsQueryToken token) const {
        return token.owner == owner_ && token.generation == generation_;
    }

    bool raycast(PhysicsQueryToken token, const XMFLOAT3& origin,
        const XMFLOAT3& direction, float max_distance, uint32_t layer_mask,
        PhysicsQueryHit& hit, uint32_t filter_mask = wi::enums::FILTER_COLLIDER |
            wi::enums::FILTER_OBJECT_ALL) const {
        if (!valid(token) || !finite_vector(origin) || !finite_vector(direction) ||
            !std::isfinite(max_distance) || max_distance <= 0.0f ||
            length_squared(direction) <= 0.000001f) return false;
        const wi::primitive::Ray ray(origin, direction, 0.0f, max_distance);
        const auto result = scene_.Intersects(ray, filter_mask, layer_mask);
        if (result.entity == wi::ecs::INVALID_ENTITY || result.distance > max_distance) return false;
        hit = {result.entity, result.position, result.normal, result.distance, 0.0f};
        return true;
    }

    size_t raycast_all(PhysicsQueryToken token, const XMFLOAT3& origin,
        const XMFLOAT3& direction, float max_distance, uint32_t layer_mask,
        Hits& hits, uint32_t filter_mask = wi::enums::FILTER_COLLIDER |
            wi::enums::FILTER_OBJECT_ALL) const {
        hits.count = 0;
        if (!valid(token) || !finite_vector(origin) || !finite_vector(direction) ||
            !std::isfinite(max_distance) || max_distance <= 0.0f ||
            length_squared(direction) <= 0.000001f) return 0;
        wi::vector<wi::scene::Scene::RayIntersectionResult> results;
        const wi::primitive::Ray ray(origin, direction, 0.0f, max_distance);
        scene_.IntersectsAll(results, ray, filter_mask, layer_mask);
        for (const auto& result : results) {
            if (hits.count == MAX_HITS || result.entity == wi::ecs::INVALID_ENTITY ||
                result.distance > max_distance) break;
            hits.values[hits.count++] = {
                result.entity, result.position, result.normal, result.distance, 0.0f};
        }
        return hits.count;
    }

    bool overlap_sphere(PhysicsQueryToken token, const XMFLOAT3& center,
        float radius, uint32_t layer_mask, PhysicsQueryHit& hit,
        uint32_t filter_mask = wi::enums::FILTER_COLLIDER |
            wi::enums::FILTER_OBJECT_ALL) const {
        if (!valid(token) || !finite_vector(center) || !std::isfinite(radius) || radius <= 0.0f) {
            return false;
        }
        const auto result = scene_.Intersects(wi::primitive::Sphere(center, radius), filter_mask, layer_mask);
        if (result.entity == wi::ecs::INVALID_ENTITY) return false;
        hit = {result.entity, result.position, result.normal, 0.0f, result.depth};
        return true;
    }

    size_t overlap_sphere_all(PhysicsQueryToken token, const XMFLOAT3& center,
        float radius, uint32_t layer_mask, Hits& hits,
        uint32_t filter_mask = wi::enums::FILTER_COLLIDER |
            wi::enums::FILTER_OBJECT_ALL) const {
        hits.count = 0;
        if (!valid(token) || !finite_vector(center) || !std::isfinite(radius) || radius <= 0.0f) {
            return 0;
        }
        wi::vector<wi::scene::Scene::SphereIntersectionResult> results;
        scene_.IntersectsAll(results, wi::primitive::Sphere(center, radius), filter_mask, layer_mask);
        append_overlaps(results, hits);
        return hits.count;
    }

    bool overlap_capsule(PhysicsQueryToken token, const XMFLOAT3& base,
        const XMFLOAT3& tip, float radius, uint32_t layer_mask, PhysicsQueryHit& hit,
        uint32_t filter_mask = wi::enums::FILTER_COLLIDER |
            wi::enums::FILTER_OBJECT_ALL) const {
        if (!valid(token) || !finite_vector(base) || !finite_vector(tip) ||
            !std::isfinite(radius) || radius <= 0.0f) return false;
        const auto result = scene_.Intersects(
            wi::primitive::Capsule(base, tip, radius), filter_mask, layer_mask);
        if (result.entity == wi::ecs::INVALID_ENTITY) return false;
        hit = {result.entity, result.position, result.normal, 0.0f, result.depth};
        return true;
    }

    size_t overlap_capsule_all(PhysicsQueryToken token, const XMFLOAT3& base,
        const XMFLOAT3& tip, float radius, uint32_t layer_mask, Hits& hits,
        uint32_t filter_mask = wi::enums::FILTER_COLLIDER |
            wi::enums::FILTER_OBJECT_ALL) const {
        hits.count = 0;
        if (!valid(token) || !finite_vector(base) || !finite_vector(tip) ||
            !std::isfinite(radius) || radius <= 0.0f) return 0;
        wi::vector<wi::scene::Scene::CapsuleIntersectionResult> results;
        scene_.IntersectsAll(results, wi::primitive::Capsule(base, tip, radius), filter_mask, layer_mask);
        append_overlaps(results, hits);
        return hits.count;
    }

private:
    template <typename Result>
    static void append_overlaps(const wi::vector<Result>& results, Hits& hits) {
        for (const auto& result : results) {
            if (hits.count == MAX_HITS) break;
            if (result.entity == wi::ecs::INVALID_ENTITY || contains_entity(hits, result.entity)) {
                continue;
            }
            hits.values[hits.count++] = {
                result.entity, result.position, result.normal, 0.0f, result.depth};
        }
    }

    static bool finite_vector(const XMFLOAT3& value) {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    }

    static float length_squared(const XMFLOAT3& value) {
        return value.x * value.x + value.y * value.y + value.z * value.z;
    }

    wi::scene::Scene& scene_;
    uintptr_t owner_;
    uint32_t generation_ = 1;
};

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
        !check(!bridge.raycast(token, XMFLOAT3(0, 0, -4), XMFLOAT3(0, 0, 1), 10.0f,
            1u << 1, hit), "query layer filter") ||
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
