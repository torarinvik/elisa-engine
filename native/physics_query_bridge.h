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

enum class PhysicsContactKind : uint8_t { Added, Persisted, Removed };

struct PhysicsContactEvent {
    uint64_t entity_a = 0;
    uint64_t entity_b = 0;
    XMFLOAT3 position = XMFLOAT3(0, 0, 0);
    XMFLOAT3 normal = XMFLOAT3(0, 0, 0);
    float depth = 0.0f;
    PhysicsContactKind kind = PhysicsContactKind::Added;
    bool trigger = false;
    uint64_t sequence = 0;
};

class PhysicsContactQueue {
public:
    static constexpr size_t MAX_EVENTS = 64;

    bool reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (active_ || dispatching_) return false;
        events_.fill({});
        owner_thread_ = std::thread::id();
        last_tick_ = UINT64_MAX;
        next_sequence_ = 1;
        event_count_ = 0;
        dropped_ = 0;
        delivered_ = 0;
        return true;
    }

    bool begin_step(uint64_t tick) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (active_ || (last_tick_ != UINT64_MAX && tick <= last_tick_)) return false;
        events_.fill({});
        event_count_ = 0;
        dropped_ = 0;
        delivered_ = 0;
        next_sequence_ = 1;
        last_tick_ = tick;
        owner_thread_ = std::this_thread::get_id();
        active_ = true;
        dispatching_ = false;
        return true;
    }

    bool publish(PhysicsContactEvent event) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!active_ || event.entity_a == 0 || event.entity_b == 0 ||
            event.entity_a == event.entity_b || !finite_vector(event.position) ||
            !finite_vector(event.normal) || !std::isfinite(event.depth) || event.depth < 0.0f) {
            return false;
        }
        if (event_count_ == MAX_EVENTS) {
            ++dropped_;
            return false;
        }
        if (event.entity_b < event.entity_a) {
            std::swap(event.entity_a, event.entity_b);
            event.normal.x = -event.normal.x;
            event.normal.y = -event.normal.y;
            event.normal.z = -event.normal.z;
        }
        event.sequence = next_sequence_++;
        events_[event_count_++] = event;
        return true;
    }

    template <typename Callback>
    size_t drain(Callback callback) {
        std::array<PhysicsContactEvent, MAX_EVENTS> ready{};
        size_t ready_count = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!active_ || dispatching_ || std::this_thread::get_id() != owner_thread_) return 0;
            ready_count = event_count_;
            std::copy_n(events_.begin(), ready_count, ready.begin());
            event_count_ = 0;
            dispatching_ = true;
        }
        std::sort(ready.begin(), ready.begin() + ready_count, [](const auto& left, const auto& right) {
            if (left.entity_a != right.entity_a) return left.entity_a < right.entity_a;
            if (left.entity_b != right.entity_b) return left.entity_b < right.entity_b;
            if (left.kind != right.kind) {
                return static_cast<uint8_t>(left.kind) < static_cast<uint8_t>(right.kind);
            }
            return left.sequence < right.sequence;
        });
        for (size_t index = 0; index < ready_count; ++index) callback(ready[index]);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            delivered_ += ready_count;
            dispatching_ = false;
        }
        return ready_count;
    }

    bool end_step() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!active_ || dispatching_ || std::this_thread::get_id() != owner_thread_) return false;
        active_ = false;
        return true;
    }

    size_t event_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return event_count_;
    }

    size_t dropped() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return dropped_;
    }

    size_t delivered() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return delivered_;
    }

private:
    static bool finite_vector(const XMFLOAT3& value) {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    }

    mutable std::mutex mutex_;
    std::array<PhysicsContactEvent, MAX_EVENTS> events_{};
    std::thread::id owner_thread_;
    uint64_t last_tick_ = UINT64_MAX;
    uint64_t next_sequence_ = 1;
    size_t event_count_ = 0;
    size_t dropped_ = 0;
    size_t delivered_ = 0;
    bool active_ = false;
    bool dispatching_ = false;
};

class PhysicsContactQueueListener final : public wi::physics::ContactEventListener {
public:
    explicit PhysicsContactQueueListener(PhysicsContactQueue& queue) : queue_(queue) {}

    void OnContact(const wi::physics::ContactEvent& event) override {
        queue_.publish({event.entity_a, event.entity_b, event.position, event.normal,
            event.penetration_depth, kind(event.type), event.sensor, 0});
    }

private:
    static PhysicsContactKind kind(wi::physics::ContactEventType type) {
        switch (type) {
        case wi::physics::ContactEventType::Persisted: return PhysicsContactKind::Persisted;
        case wi::physics::ContactEventType::Removed: return PhysicsContactKind::Removed;
        case wi::physics::ContactEventType::Added: break;
        }
        return PhysicsContactKind::Added;
    }

    PhysicsContactQueue& queue_;
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
        std::array<wi::physics::RayIntersectionResult, MAX_HITS> physics_results{};
        const size_t physics_count = wi::physics::IntersectsAll(scene_, ray, layer_mask,
            physics_results.data(), physics_results.size());
        const float inverse_length = 1.0f / std::sqrt(length_squared(direction));
        const XMFLOAT3 unit_direction = XMFLOAT3(direction.x * inverse_length,
            direction.y * inverse_length, direction.z * inverse_length);
        for (size_t index = 0; index < physics_count && hits.count < MAX_HITS; ++index) {
            const auto& result = physics_results[index];
            if (!result.IsValid() || contains_entity(hits, result.entity)) continue;
            const XMFLOAT3 offset = XMFLOAT3(result.position.x - origin.x,
                result.position.y - origin.y, result.position.z - origin.z);
            const float distance = offset.x * unit_direction.x + offset.y * unit_direction.y +
                offset.z * unit_direction.z;
            if (!std::isfinite(distance) || distance < 0.0f || distance > max_distance) continue;
            hits.values[hits.count++] = {
                result.entity, result.position, result.normal, distance, 0.0f};
        }
        std::sort(hits.values.begin(), hits.values.begin() + hits.count,
            [](const PhysicsQueryHit& left, const PhysicsQueryHit& right) {
                return left.distance < right.distance;
            });
        return hits.count;
    }

    bool sphere_cast(PhysicsQueryToken token, const XMFLOAT3& center,
        const XMFLOAT3& direction, float max_distance, float radius,
        uint32_t layer_mask, PhysicsQueryHit& hit,
        uint32_t filter_mask = wi::enums::FILTER_COLLIDER |
            wi::enums::FILTER_OBJECT_ALL) const {
        if (!finite_vector(center) || !std::isfinite(radius) || radius <= 0.0f) {
            return false;
        }
        return cast_shape(token, direction, max_distance, hit,
            [this, token, center, radius, layer_mask, filter_mask](const XMFLOAT3& offset,
                PhysicsQueryHit& candidate) {
                return overlap_sphere(token,
                    XMFLOAT3(center.x + offset.x, center.y + offset.y, center.z + offset.z),
                    radius, layer_mask, candidate, filter_mask);
            });
    }

    bool capsule_cast(PhysicsQueryToken token, const XMFLOAT3& base,
        const XMFLOAT3& tip, const XMFLOAT3& direction, float max_distance,
        float radius, uint32_t layer_mask, PhysicsQueryHit& hit,
        uint32_t filter_mask = wi::enums::FILTER_COLLIDER |
            wi::enums::FILTER_OBJECT_ALL) const {
        if (!finite_vector(base) || !finite_vector(tip) || !std::isfinite(radius) ||
            radius <= 0.0f) return false;
        return cast_shape(token, direction, max_distance, hit,
            [this, token, base, tip, radius, layer_mask, filter_mask](const XMFLOAT3& offset,
                PhysicsQueryHit& candidate) {
                return overlap_capsule(token,
                    XMFLOAT3(base.x + offset.x, base.y + offset.y, base.z + offset.z),
                    XMFLOAT3(tip.x + offset.x, tip.y + offset.y, tip.z + offset.z),
                    radius, layer_mask, candidate, filter_mask);
            });
    }

    bool overlap_sphere(PhysicsQueryToken token, const XMFLOAT3& center,
        float radius, uint32_t layer_mask, PhysicsQueryHit& hit,
        uint32_t filter_mask = wi::enums::FILTER_COLLIDER |
            wi::enums::FILTER_OBJECT_ALL) const {
        if (!valid(token) || !finite_vector(center) || !std::isfinite(radius) || radius <= 0.0f) {
            return false;
        }
        const auto result = scene_.Intersects(wi::primitive::Sphere(center, radius), filter_mask, layer_mask);
        if (result.entity != wi::ecs::INVALID_ENTITY) {
            hit = {result.entity, result.position, result.normal, 0.0f, result.depth};
        }
        wi::physics::ShapeIntersectionResult physics_result;
        if (wi::physics::Intersects(scene_, wi::primitive::Sphere(center, radius),
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
            wi::enums::FILTER_OBJECT_ALL) const {
        hits.count = 0;
        if (!valid(token) || !finite_vector(center) || !std::isfinite(radius) || radius <= 0.0f) {
            return 0;
        }
        wi::vector<wi::scene::Scene::SphereIntersectionResult> results;
        scene_.IntersectsAll(results, wi::primitive::Sphere(center, radius), filter_mask, layer_mask);
        append_overlaps(results, hits);
        wi::physics::ShapeIntersectionResult physics_results[MAX_HITS];
        const size_t physics_count = wi::physics::IntersectsAll(scene_,
            wi::primitive::Sphere(center, radius), layer_mask, physics_results, MAX_HITS);
        append_physics_overlaps(physics_results, physics_count, hits);
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
        if (result.entity != wi::ecs::INVALID_ENTITY) {
            hit = {result.entity, result.position, result.normal, 0.0f, result.depth};
        }
        wi::physics::ShapeIntersectionResult physics_result;
        if (wi::physics::Intersects(scene_, wi::primitive::Capsule(base, tip, radius),
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
            wi::enums::FILTER_OBJECT_ALL) const {
        hits.count = 0;
        if (!valid(token) || !finite_vector(base) || !finite_vector(tip) ||
            !std::isfinite(radius) || radius <= 0.0f) return 0;
        wi::vector<wi::scene::Scene::CapsuleIntersectionResult> results;
        scene_.IntersectsAll(results, wi::primitive::Capsule(base, tip, radius), filter_mask, layer_mask);
        append_overlaps(results, hits);
        wi::physics::ShapeIntersectionResult physics_results[MAX_HITS];
        const size_t physics_count = wi::physics::IntersectsAll(scene_,
            wi::primitive::Capsule(base, tip, radius), layer_mask, physics_results, MAX_HITS);
        append_physics_overlaps(physics_results, physics_count, hits);
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
                if (probe(middle_offset, candidate)) {
                    upper = middle;
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

inline bool probe_physics_contact_queue() {
    PhysicsContactQueue queue;
    if (!check(queue.begin_step(1) && !queue.begin_step(1), "contact queue step ownership")) {
        return false;
    }
    size_t worker_drain = 0;
    std::thread worker([&queue, &worker_drain] {
        queue.publish({7, 3, XMFLOAT3(0, 0, 0), XMFLOAT3(0, 1, 0), 0.25f,
            PhysicsContactKind::Added, false, 0});
        queue.publish({2, 9, XMFLOAT3(1, 0, 0), XMFLOAT3(0, 1, 0), 0.5f,
            PhysicsContactKind::Removed, true, 0});
        for (uint64_t index = 0; index < PhysicsContactQueue::MAX_EVENTS; ++index) {
            queue.publish({100 + index, 200 + index, XMFLOAT3(0, 0, 0),
                XMFLOAT3(0, 1, 0), 0.0f, PhysicsContactKind::Persisted, false, 0});
        }
        worker_drain = queue.drain([](const PhysicsContactEvent&) {});
    });
    worker.join();
    if (!check(worker_drain == 0 && queue.event_count() == PhysicsContactQueue::MAX_EVENTS &&
            queue.dropped() == 2, "contact queue worker handoff and overflow")) return false;
    bool ordered = true;
    bool reentrant_blocked = false;
    uint64_t previous_a = 0;
    uint64_t previous_b = 0;
    const size_t drained = queue.drain([&](const PhysicsContactEvent& event) {
        if (event.entity_a < previous_a ||
            (event.entity_a == previous_a && event.entity_b < previous_b)) ordered = false;
        previous_a = event.entity_a;
        previous_b = event.entity_b;
        reentrant_blocked = queue.drain([](const PhysicsContactEvent&) {}) == 0;
    });
    if (!check(drained == PhysicsContactQueue::MAX_EVENTS && ordered && reentrant_blocked &&
            queue.delivered() == PhysicsContactQueue::MAX_EVENTS,
            "contact queue deterministic main-thread delivery")) return false;
    if (!check(queue.end_step() && !queue.publish({3, 4, XMFLOAT3(0, 0, 0), XMFLOAT3(0, 1, 0),
            0.1f, PhysicsContactKind::Added, false, 0}) && !queue.begin_step(1) &&
            queue.begin_step(2) && queue.end_step(), "contact queue close and tick ordering")) {
        return false;
    }
    return true;
}

} // namespace probe
