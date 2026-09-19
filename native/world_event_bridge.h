#pragma once

// Main-thread delivery boundary for portable world events. Producers may emit
// from worker threads, while phase advancement and callbacks stay serialized.
#include "probe_core.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>

namespace probe {

enum class NativeEventPhase : uint8_t { Input, Simulation, Physics, Animation, Render, Audio };
enum class NativeEventKind : uint8_t { Spawn, Despawn, Damage, AudioCue };

struct NativeWorldEvent {
    NativeEventPhase phase = NativeEventPhase::Input;
    NativeEventKind kind = NativeEventKind::Spawn;
    uint32_t listener = 0;
    uint64_t sequence = 0;
    int64_t entity = 0;
    int64_t payload = 0;
};

class NativeWorldEventBridge {
public:
    static constexpr size_t MAX_EVENTS = 64;
    static constexpr size_t MAX_LISTENERS = 16;

    bool begin_frame() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (active_) return false;
        events_.fill({});
        delivered_flags_.fill(false);
        listeners_.fill({});
        event_count_ = 0;
        delivered_ = 0;
        phase_ = NativeEventPhase::Input;
        active_ = true;
        return true;
    }

    bool subscribe(uint32_t listener, NativeEventPhase phase) {
        if (listener == 0) return false;
        std::lock_guard<std::mutex> lock(mutex_);
        if (!active_ || find_listener(listener) != MAX_LISTENERS) return false;
        for (auto& slot : listeners_) {
            if (!slot.live) {
                slot = {listener, phase, true};
                return true;
            }
        }
        return false;
    }

    bool unsubscribe(uint32_t listener) {
        std::lock_guard<std::mutex> lock(mutex_);
        const size_t slot = find_listener(listener);
        if (slot == MAX_LISTENERS) return false;
        listeners_[slot].live = false;
        return true;
    }

    bool emit(NativeWorldEvent event) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!active_ || event.entity <= 0 || event.listener == 0 ||
            phase_value(event.phase) < phase_value(phase_) ||
            find_listener(event.listener) == MAX_LISTENERS || event_count_ == MAX_EVENTS) return false;
        event.sequence = next_sequence_++;
        events_[event_count_++] = event;
        return true;
    }

    bool advance(NativeEventPhase next) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!active_ || phase_value(next) != phase_value(phase_) + 1) return false;
        phase_ = next;
        return true;
    }

    template <typename Callback>
    size_t dispatch(NativeEventPhase phase, Callback callback) {
        std::array<NativeWorldEvent, MAX_EVENTS> ready{};
        size_t ready_count = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!active_ || phase != phase_) return 0;
            for (size_t index = 0; index < event_count_ && ready_count < MAX_EVENTS; ++index) {
                NativeWorldEvent& event = events_[index];
                const size_t listener = find_listener(event.listener);
                if (event.phase == phase && listener != MAX_LISTENERS &&
                    listeners_[listener].phase == phase && !delivered_flags_[index]) {
                    delivered_flags_[index] = true;
                    ready[ready_count++] = event;
                }
            }
            delivered_ += ready_count;
        }
        for (size_t index = 0; index < ready_count; ++index) callback(ready[index]);
        return ready_count;
    }

    size_t end_frame() {
        std::lock_guard<std::mutex> lock(mutex_);
        active_ = false;
        return delivered_;
    }

    NativeEventPhase phase() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return phase_;
    }

    size_t event_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return event_count_;
    }

private:
    struct Listener {
        uint32_t id = 0;
        NativeEventPhase phase = NativeEventPhase::Input;
        bool live = false;
    };

    static uint8_t phase_value(NativeEventPhase phase) {
        return static_cast<uint8_t>(phase);
    }

    size_t find_listener(uint32_t listener) const {
        for (size_t index = 0; index < MAX_LISTENERS; ++index) {
            if (listeners_[index].live && listeners_[index].id == listener) return index;
        }
        return MAX_LISTENERS;
    }

    mutable std::mutex mutex_;
    std::array<NativeWorldEvent, MAX_EVENTS> events_{};
    std::array<bool, MAX_EVENTS> delivered_flags_{};
    std::array<Listener, MAX_LISTENERS> listeners_{};
    NativeEventPhase phase_ = NativeEventPhase::Input;
    uint64_t next_sequence_ = 1;
    size_t event_count_ = 0;
    size_t delivered_ = 0;
    bool active_ = false;
};

inline bool probe_world_event_bridge() {
    NativeWorldEventBridge bridge;
    if (!check(bridge.begin_frame() && bridge.subscribe(7, NativeEventPhase::Simulation),
        "native event frame and subscription")) return false;
    std::thread worker([&bridge] {
        bridge.emit({NativeEventPhase::Simulation, NativeEventKind::Damage, 7, 0, 42, 3});
    });
    worker.join();
    if (!check(bridge.event_count() == 1 && !bridge.advance(NativeEventPhase::Physics),
        "native event worker handoff and phase guard")) return false;
    if (!check(bridge.advance(NativeEventPhase::Simulation), "native event phase advance")) return false;
    size_t delivered = 0;
    const size_t dispatched = bridge.dispatch(NativeEventPhase::Simulation, [&](const NativeWorldEvent& event) {
        delivered += event.entity == 42 && event.payload == 3 ? 1 : 0;
    });
    if (!check(dispatched == 1 && delivered == 1 && bridge.dispatch(NativeEventPhase::Simulation,
        [](const NativeWorldEvent&) {}) == 0, "native event main-thread delivery")) return false;
    if (!check(bridge.unsubscribe(7) && !bridge.emit({NativeEventPhase::Physics, NativeEventKind::Damage,
        7, 0, 42, 4}), "native event unsubscribe")) return false;
    if (!check(!bridge.advance(NativeEventPhase::Audio) && bridge.end_frame() == 1,
        "native event phase order and close")) return false;
    return true;
}

} // namespace probe
