#pragma once

// Bounded worker-to-owner delivery for Jolt contact events. Native physics
// types stay inside this adapter; consumers receive stable copied records.
#include "probe_core.h"
#include "wiPhysics.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <thread>

namespace probe {

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
