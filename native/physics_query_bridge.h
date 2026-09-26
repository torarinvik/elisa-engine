#pragma once

// Bounded scene collision queries for the Elisa/native boundary. Wicked keeps
// its entity and intersection types private to this adapter; callers receive
// only copied hit data and a generation-checked query token.
#include "probe_core.h"
#include "wiScene.h"
#include "wiPhysics.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <thread>

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
    static constexpr size_t MAX_CAST_STEPS = 128;
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

    bool raycast_physics(PhysicsQueryToken token, const XMFLOAT3& origin,
        const XMFLOAT3& direction, float max_distance, uint32_t layer_mask,
        PhysicsQueryHit& hit) const {
        if (!valid(token) || layer_mask == 0 || !finite_vector(origin) ||
            !finite_vector(direction) || !std::isfinite(max_distance) ||
            max_distance <= 0.0f || length_squared(direction) <= 0.000001f) return false;
        const float inverse_length = 1.0f / std::sqrt(length_squared(direction));
        const XMFLOAT3 unit_direction = XMFLOAT3(
            direction.x * inverse_length, direction.y * inverse_length,
            direction.z * inverse_length);
        const wi::physics::RayIntersectionResult result = wi::physics::Intersects(
            scene_, wi::primitive::Ray(origin, direction, 0.0f, max_distance), layer_mask);
        if (!result.IsValid()) return false;
        const XMFLOAT3 offset = XMFLOAT3(result.position.x - origin.x,
            result.position.y - origin.y, result.position.z - origin.z);
        const float distance = offset.x * unit_direction.x +
            offset.y * unit_direction.y + offset.z * unit_direction.z;
        if (!std::isfinite(distance) || distance < 0.0f || distance > max_distance) return false;
        hit = {result.entity, result.position, result.normal, distance, 0.0f};
        return true;
    }

    bool raycast_filtered(PhysicsQueryToken token, const XMFLOAT3& origin,
        const XMFLOAT3& direction, float max_distance, uint32_t layer_mask,
        uint32_t scene_filter_mask, bool include_physics_bodies,
        PhysicsQueryHit& hit) const {
        if (!valid(token) || !finite_vector(origin) || !finite_vector(direction) ||
            !std::isfinite(max_distance) || max_distance <= 0.0f ||
            length_squared(direction) <= 0.000001f) return false;
        bool found = false;
        PhysicsQueryHit nearest{};
        if (scene_filter_mask != 0) {
            PhysicsQueryHit scene_hit{};
            if (raycast(token, origin, direction, max_distance, layer_mask,
                    scene_hit, scene_filter_mask)) {
                nearest = scene_hit;
                found = true;
            }
        }
        if (include_physics_bodies) {
            PhysicsQueryHit physics_hit{};
            if (raycast_physics(token, origin, direction, max_distance,
                    layer_mask, physics_hit) &&
                (!found || physics_hit.distance < nearest.distance)) {
                nearest = physics_hit;
                found = true;
            }
        }
        if (found) hit = nearest;
        return found;
    }

    size_t raycast_all(PhysicsQueryToken token, const XMFLOAT3& origin,
        const XMFLOAT3& direction, float max_distance, uint32_t layer_mask,
        Hits& hits, uint32_t filter_mask = wi::enums::FILTER_COLLIDER |
            wi::enums::FILTER_OBJECT_ALL, bool include_physics_bodies = true) const {
        hits.count = 0;
        if (!valid(token) || !finite_vector(origin) || !finite_vector(direction) ||
            !std::isfinite(max_distance) || max_distance <= 0.0f ||
            length_squared(direction) <= 0.000001f) return 0;
        wi::vector<wi::scene::Scene::RayIntersectionResult> results;
        const wi::primitive::Ray ray(origin, direction, 0.0f, max_distance);
        scene_.IntersectsAll(results, ray, filter_mask, layer_mask);
        for (const auto& result : results) {
            if (result.entity == wi::ecs::INVALID_ENTITY || result.distance > max_distance) continue;
            insert_nearest(hits, {result.entity, result.position, result.normal,
                result.distance, 0.0f});
        }
        std::array<wi::physics::RayIntersectionResult, MAX_HITS> physics_results{};
        const size_t physics_count = include_physics_bodies
            ? wi::physics::IntersectsAll(scene_, ray, layer_mask,
                physics_results.data(), physics_results.size()) : 0;
        const float inverse_length = 1.0f / std::sqrt(length_squared(direction));
        const XMFLOAT3 unit_direction = XMFLOAT3(direction.x * inverse_length,
            direction.y * inverse_length, direction.z * inverse_length);
        for (size_t index = 0; index < physics_count; ++index) {
            const auto& result = physics_results[index];
            if (!result.IsValid()) continue;
            const XMFLOAT3 offset = XMFLOAT3(result.position.x - origin.x,
                result.position.y - origin.y, result.position.z - origin.z);
            const float distance = offset.x * unit_direction.x + offset.y * unit_direction.y +
                offset.z * unit_direction.z;
            if (!std::isfinite(distance) || distance < 0.0f || distance > max_distance) continue;
            insert_nearest(hits, {result.entity, result.position, result.normal,
                distance, 0.0f});
        }
        std::sort(hits.values.begin(), hits.values.begin() + hits.count,
            [](const PhysicsQueryHit& left, const PhysicsQueryHit& right) {
                return precedes(left, right);
            });
        return hits.count;
    }

    bool sphere_cast(PhysicsQueryToken token, const XMFLOAT3& center,
        const XMFLOAT3& direction, float max_distance, float radius,
        uint32_t layer_mask, PhysicsQueryHit& hit,
        uint32_t filter_mask = wi::enums::FILTER_COLLIDER |
            wi::enums::FILTER_OBJECT_ALL, bool include_physics_bodies = true) const {
        if (!finite_vector(center) || !std::isfinite(radius) || radius <= 0.0f) {
            return false;
        }
        return cast_shape(token, direction, max_distance, hit,
            [this, token, center, radius, layer_mask, filter_mask, include_physics_bodies](const XMFLOAT3& offset,
                PhysicsQueryHit& candidate) {
                return overlap_sphere(token,
                    XMFLOAT3(center.x + offset.x, center.y + offset.y, center.z + offset.z),
                    radius, layer_mask, candidate, filter_mask, include_physics_bodies);
            });
    }

    bool capsule_cast(PhysicsQueryToken token, const XMFLOAT3& base,
        const XMFLOAT3& tip, const XMFLOAT3& direction, float max_distance,
        float radius, uint32_t layer_mask, PhysicsQueryHit& hit,
        uint32_t filter_mask = wi::enums::FILTER_COLLIDER |
            wi::enums::FILTER_OBJECT_ALL, bool include_physics_bodies = true) const {
        if (!finite_vector(base) || !finite_vector(tip) || !std::isfinite(radius) ||
            radius <= 0.0f) return false;
        return cast_shape(token, direction, max_distance, hit,
            [this, token, base, tip, radius, layer_mask, filter_mask, include_physics_bodies](const XMFLOAT3& offset,
                PhysicsQueryHit& candidate) {
                return overlap_capsule(token,
                    XMFLOAT3(base.x + offset.x, base.y + offset.y, base.z + offset.z),
                    XMFLOAT3(tip.x + offset.x, tip.y + offset.y, tip.z + offset.z),
                    radius, layer_mask, candidate, filter_mask, include_physics_bodies);
            });
    }

    bool overlap_sphere(PhysicsQueryToken token, const XMFLOAT3& center,
        float radius, uint32_t layer_mask, PhysicsQueryHit& hit,
        uint32_t filter_mask = wi::enums::FILTER_COLLIDER |
            wi::enums::FILTER_OBJECT_ALL, bool include_physics_bodies = true) const {
        if (!valid(token) || !finite_vector(center) || !std::isfinite(radius) || radius <= 0.0f) {
            return false;
        }
        hit = {};
        overlap_scene<wi::scene::Scene::SphereIntersectionResult>(
            wi::primitive::Sphere(center, radius), filter_mask, layer_mask, hit);
        wi::physics::ShapeIntersectionResult physics_result;
        if (include_physics_bodies && wi::physics::Intersects(scene_, wi::primitive::Sphere(center, radius),
                layer_mask, physics_result) &&
            (hit.entity == wi::ecs::INVALID_ENTITY || physics_result.depth > hit.depth)) {
            hit = {physics_result.entity, physics_result.position, physics_result.normal,
                0.0f, physics_result.depth};
        }
        return hit.entity != wi::ecs::INVALID_ENTITY;
    }

    size_t overlap_sphere_all(PhysicsQueryToken token, const XMFLOAT3& center,
        float radius, uint32_t layer_mask, Hits& hits,
        uint32_t filter_mask = wi::enums::FILTER_COLLIDER |
            wi::enums::FILTER_OBJECT_ALL, bool include_physics_bodies = true) const {
        hits.count = 0;
        if (!valid(token) || !finite_vector(center) || !std::isfinite(radius) || radius <= 0.0f) {
            return 0;
        }
        wi::vector<wi::scene::Scene::SphereIntersectionResult> results;
        scene_.IntersectsAll(results, wi::primitive::Sphere(center, radius), filter_mask, layer_mask);
        append_overlaps(results, hits);
        wi::physics::ShapeIntersectionResult physics_results[MAX_HITS];
        const size_t physics_count = include_physics_bodies ? wi::physics::IntersectsAll(scene_,
            wi::primitive::Sphere(center, radius), layer_mask, physics_results, MAX_HITS) : 0;
        append_physics_overlaps(physics_results, physics_count, hits);
        return hits.count;
    }

    bool overlap_capsule(PhysicsQueryToken token, const XMFLOAT3& base,
        const XMFLOAT3& tip, float radius, uint32_t layer_mask, PhysicsQueryHit& hit,
        uint32_t filter_mask = wi::enums::FILTER_COLLIDER |
            wi::enums::FILTER_OBJECT_ALL, bool include_physics_bodies = true) const {
        if (!valid(token) || !finite_vector(base) || !finite_vector(tip) ||
            !std::isfinite(radius) || radius <= 0.0f) return false;
        hit = {};
        overlap_scene<wi::scene::Scene::CapsuleIntersectionResult>(
            wi::primitive::Capsule(base, tip, radius), filter_mask, layer_mask, hit);
        wi::physics::ShapeIntersectionResult physics_result;
        if (include_physics_bodies && wi::physics::Intersects(scene_, wi::primitive::Capsule(base, tip, radius),
                layer_mask, physics_result) &&
            (hit.entity == wi::ecs::INVALID_ENTITY || physics_result.depth > hit.depth)) {
            hit = {physics_result.entity, physics_result.position, physics_result.normal,
                0.0f, physics_result.depth};
        }
        return hit.entity != wi::ecs::INVALID_ENTITY;
    }

    size_t overlap_capsule_all(PhysicsQueryToken token, const XMFLOAT3& base,
        const XMFLOAT3& tip, float radius, uint32_t layer_mask, Hits& hits,
        uint32_t filter_mask = wi::enums::FILTER_COLLIDER |
            wi::enums::FILTER_OBJECT_ALL, bool include_physics_bodies = true) const {
        hits.count = 0;
        if (!valid(token) || !finite_vector(base) || !finite_vector(tip) ||
            !std::isfinite(radius) || radius <= 0.0f) return 0;
        wi::vector<wi::scene::Scene::CapsuleIntersectionResult> results;
        scene_.IntersectsAll(results, wi::primitive::Capsule(base, tip, radius), filter_mask, layer_mask);
        append_overlaps(results, hits);
        wi::physics::ShapeIntersectionResult physics_results[MAX_HITS];
        const size_t physics_count = include_physics_bodies ? wi::physics::IntersectsAll(scene_,
            wi::primitive::Capsule(base, tip, radius), layer_mask, physics_results, MAX_HITS) : 0;
        append_physics_overlaps(physics_results, physics_count, hits);
        return hits.count;
    }

private:
    static bool precedes(const PhysicsQueryHit& left, const PhysicsQueryHit& right) {
        if (left.distance != right.distance) return left.distance < right.distance;
        return left.entity < right.entity;
    }

    static bool insert_nearest(Hits& hits, const PhysicsQueryHit& candidate) {
        if (candidate.entity == wi::ecs::INVALID_ENTITY ||
            !std::isfinite(candidate.distance) || candidate.distance < 0.0f) return false;
        for (size_t index = 0; index < hits.count; ++index) {
            if (hits.values[index].entity != candidate.entity) continue;
            if (!precedes(candidate, hits.values[index])) return false;
            for (size_t next = index + 1; next < hits.count; ++next) {
                hits.values[next - 1] = hits.values[next];
            }
            --hits.count;
            break;
        }
        if (hits.count == MAX_HITS && !precedes(candidate, hits.values[hits.count - 1])) {
            return false;
        }
        size_t insertion = hits.count;
        if (hits.count < MAX_HITS) {
            ++hits.count;
        } else {
            insertion = MAX_HITS - 1;
        }
        while (insertion > 0 && precedes(candidate, hits.values[insertion - 1])) {
            hits.values[insertion] = hits.values[insertion - 1];
            --insertion;
        }
        hits.values[insertion] = candidate;
        return true;
    }

    template <typename Result, typename Shape>
    bool overlap_scene(const Shape& shape, uint32_t filter_mask, uint32_t layer_mask,
        PhysicsQueryHit& hit) const {
        const auto nearest = scene_.Intersects(shape, filter_mask, layer_mask);
        if (nearest.entity != wi::ecs::INVALID_ENTITY) {
            hit = {nearest.entity, nearest.position, nearest.normal, 0.0f,
                std::abs(nearest.depth)};
        }
        if ((filter_mask & wi::enums::FILTER_COLLIDER) == 0) {
            return hit.entity != wi::ecs::INVALID_ENTITY;
        }

        wi::vector<Result> collider_results;
        scene_.IntersectsAll(collider_results, shape, wi::enums::FILTER_COLLIDER, layer_mask);
        for (const auto& result : collider_results) {
            if (result.entity == wi::ecs::INVALID_ENTITY) continue;
            const float depth = std::abs(result.depth);
            if (hit.entity == wi::ecs::INVALID_ENTITY || depth > hit.depth) {
                hit = {result.entity, result.position, result.normal, 0.0f, depth};
            }
        }
        return hit.entity != wi::ecs::INVALID_ENTITY;
    }

    template <typename Result>
    static void append_overlaps(const wi::vector<Result>& results, Hits& hits) {
        for (const auto& result : results) {
            if (hits.count == MAX_HITS) break;
            if (result.entity == wi::ecs::INVALID_ENTITY || contains_entity(hits, result.entity)) {
                continue;
            }
            hits.values[hits.count++] = {
                result.entity, result.position, result.normal, 0.0f, std::abs(result.depth)};
        }
    }

    static void append_physics_overlaps(const wi::physics::ShapeIntersectionResult* results, size_t count, Hits& hits) {
        for (size_t index = 0; index < count && hits.count < MAX_HITS; ++index) {
            const auto& result = results[index];
            if (result.entity == wi::ecs::INVALID_ENTITY || contains_entity(hits, result.entity)) continue;
            hits.values[hits.count++] = {result.entity, result.position, result.normal, 0.0f, result.depth};
        }
    }

    static bool finite_vector(const XMFLOAT3& value) {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    }

    static float length_squared(const XMFLOAT3& value) {
        return value.x * value.x + value.y * value.y + value.z * value.z;
    }

    template <typename Probe>
    bool cast_shape(PhysicsQueryToken token, const XMFLOAT3& direction,
        float max_distance, PhysicsQueryHit& hit, Probe&& probe) const {
        if (!valid(token) || !finite_vector(direction) || !std::isfinite(max_distance) ||
            max_distance <= 0.0f || length_squared(direction) <= 0.000001f) return false;
        const float inverse_length = 1.0f / std::sqrt(length_squared(direction));
        const XMFLOAT3 unit_direction = XMFLOAT3(
            direction.x * inverse_length, direction.y * inverse_length, direction.z * inverse_length);
        PhysicsQueryHit candidate;
        if (probe(XMFLOAT3(0, 0, 0), candidate)) {
            hit = candidate;
            hit.distance = 0.0f;
            return true;
        }
        float previous_distance = 0.0f;
        for (size_t step = 1; step <= MAX_CAST_STEPS; ++step) {
            const float distance = max_distance * static_cast<float>(step) /
                static_cast<float>(MAX_CAST_STEPS);
            const XMFLOAT3 offset = XMFLOAT3(
                unit_direction.x * distance, unit_direction.y * distance, unit_direction.z * distance);
            if (!probe(offset, candidate)) {
                previous_distance = distance;
                continue;
            }
            float lower = previous_distance;
            float upper = distance;
            for (size_t refinement = 0; refinement < 8; ++refinement) {
                const float middle = (lower + upper) * 0.5f;
                const XMFLOAT3 middle_offset = XMFLOAT3(
                    unit_direction.x * middle, unit_direction.y * middle, unit_direction.z * middle);
                PhysicsQueryHit refined_candidate;
                if (probe(middle_offset, refined_candidate)) {
                    upper = middle;
                    candidate = refined_candidate;
                } else {
                    lower = middle;
                }
            }
            hit = candidate;
            hit.distance = upper;
            return true;
        }
        return false;
    }

    wi::scene::Scene& scene_;
    uintptr_t owner_;
    uint32_t generation_ = 1;
};

} // namespace probe
