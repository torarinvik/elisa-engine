#pragma once

// Generation/owner checked voice handles over the real bounded miniaudio
// service. The service owns callback state; this adapter owns the game-facing
// identity and can defer stop until an audio submission boundary.
#include "miniaudio_service.h"
#include "resource_handles.h"

#include <array>
#include <cstdint>
#include <vector>

namespace probe {

class NativeVoiceRegistry {
public:
    static constexpr uint32_t MAX_VOICES = audio::MAX_VOICES;

    explicit NativeVoiceRegistry(audio::Service& service)
        : service_(service), owner_(reinterpret_cast<uintptr_t>(&service)) {}

    NativeResourceHandle play(audio::ClipHandle clip) {
        for (uint32_t slot = 0; slot < MAX_VOICES; ++slot) {
            VoiceSlot& state = voices_[slot];
            if (state.live || state.retired) continue;
            const audio::VoiceHandle voice = service_.play(clip);
            if (voice.slot >= audio::MAX_VOICES || !next_generation(state)) return {};
            state.voice = voice;
            state.live = true;
            return NativeResourceHandle{slot, state.generation, owner_, NativeResourceKind::Voice};
        }
        return {};
    }

    bool is_live(NativeResourceHandle handle) const {
        const VoiceSlot* state = state_for(handle);
        return state != nullptr && state->live && state->generation == handle.generation &&
            service_.voice_live(state->voice);
    }

    bool destroy(NativeResourceHandle handle) {
        VoiceSlot* state = state_for(handle);
        if (state == nullptr || !is_live(handle)) return false;
        service_.stop(state->voice);
        state->live = false;
        return true;
    }

    bool destroy_deferred(NativeResourceHandle handle, uint64_t submission_serial) {
        VoiceSlot* state = state_for(handle);
        if (state == nullptr || !is_live(handle)) return false;
        state->live = false;
        state->retired = true;
        retired_.push_back({handle.slot, submission_serial});
        return true;
    }

    void collect_retired(uint64_t completed_serial = UINT64_MAX) {
        size_t write = 0;
        for (const Retired& resource : retired_) {
            if (resource.serial > completed_serial) {
                retired_[write++] = resource;
                continue;
            }
            VoiceSlot& state = voices_[resource.slot];
            service_.stop(state.voice);
            state.voice = {};
            state.retired = false;
        }
        retired_.resize(write);
    }

    bool force_generation_for_test(uint32_t slot, uint32_t generation) {
        if (slot >= MAX_VOICES || voices_[slot].live || voices_[slot].retired) return false;
        voices_[slot].generation = generation;
        return true;
    }

private:
    struct VoiceSlot {
        audio::VoiceHandle voice;
        uint32_t generation = 0;
        bool live = false;
        bool retired = false;
    };

    struct Retired {
        uint32_t slot = NativeResourceHandle::INVALID_SLOT;
        uint64_t serial = 0;
    };

    VoiceSlot* state_for(NativeResourceHandle handle) {
        return handle.owner == owner_ && handle.kind == NativeResourceKind::Voice &&
                handle.slot < MAX_VOICES ? &voices_[handle.slot] : nullptr;
    }

    const VoiceSlot* state_for(NativeResourceHandle handle) const {
        return handle.owner == owner_ && handle.kind == NativeResourceKind::Voice &&
                handle.slot < MAX_VOICES ? &voices_[handle.slot] : nullptr;
    }

    bool next_generation(VoiceSlot& state) {
        if (state.generation == UINT32_MAX) return false;
        state.generation = state.generation == 0 ? 1 : state.generation + 1;
        return true;
    }

    audio::Service& service_;
    uintptr_t owner_;
    std::array<VoiceSlot, MAX_VOICES> voices_{};
    std::vector<Retired> retired_;
};

} // namespace probe
