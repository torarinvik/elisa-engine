#pragma once
// Native reader for the cooked elisa-anim-v1 contract (C01, see
// src/animation/package.elisa and scripts/cook_animation_contract.py), feeding
// the C02 ozz service. Every offset is bounds-checked; magic, version, total
// length, checksum, counts, parent order, track order and key ranges are
// verified before any rig or clip is built. The Elisa decoder remains the
// authority for the full contract; this reader refuses anything it cannot
// prove in-bounds and consistent.
#include "ozz_animation_service.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace elisa::animation::anim_v1 {

constexpr uint32_t kMagic = 0x4D4E4145u;
constexpr size_t kHeaderBytes = 32, kJointBytes = 112, kClipHeaderBytes = 24,
    kTrackHeaderBytes = 8, kKeyBytes = 44, kEventBytes = 8;
constexpr uint32_t kMaxJoints = 64, kMaxClips = 24, kMaxEvents = 32;
constexpr uint32_t kMaxKeysPerClip = 262144;
constexpr size_t kMaxBytes = 64u * 1024u * 1024u;

struct Clip {
    uint32_t id = 0;
    float duration_seconds = 0.0f;
    OzzClip ozz;
};

struct Package {
    uint32_t rig_id = 0;
    std::vector<uint32_t> joint_ids;
    std::vector<int32_t> parents;
    OzzRig rig;
    std::vector<Clip> clips;
    bool valid() const { return rig.valid(); }
};

namespace detail {
inline bool word(const std::vector<uint8_t>& bytes, size_t at, uint32_t& out) {
    if (at > bytes.size() || bytes.size() - at < 4) return false;
    out = uint32_t(bytes[at]) | (uint32_t(bytes[at + 1]) << 8) |
        (uint32_t(bytes[at + 2]) << 16) | (uint32_t(bytes[at + 3]) << 24);
    return true;
}
inline bool floats(const std::vector<uint8_t>& bytes, size_t at, float* out, size_t count) {
    if (at > bytes.size() || (bytes.size() - at) / 4 < count) return false;
    std::memcpy(out, bytes.data() + at, count * 4);
    return true;
}
inline uint32_t fnv(const std::vector<uint8_t>& bytes, size_t start, size_t end) {
    uint32_t hash = 2166136261u;
    for (size_t at = start; at < end; ++at) hash = (hash ^ bytes[at]) * 16777619u;
    return hash;
}
inline uint32_t fnv(const std::vector<uint8_t>& bytes, size_t start) {
    return fnv(bytes, start, bytes.size());
}
inline bool valid_transform(const float* pose) {
    if (!elisa::animation::detail::finite_pose(pose) || pose[7] == 0.0f ||
        pose[8] == 0.0f || pose[9] == 0.0f) return false;
    const float length = std::sqrt(pose[3] * pose[3] + pose[4] * pose[4] +
        pose[5] * pose[5] + pose[6] * pose[6]);
    return std::isfinite(length) && std::abs(length - 1.0f) <= 0.002f;
}
} // namespace detail

// Parses and builds the rig and every clip. Returns an invalid Package on any
// layout, checksum, hierarchy or track violation.
inline Package load(const std::vector<uint8_t>& bytes) {
    using detail::word;
    Package package;
    uint32_t magic = 0, version = 0, total = 0, checksum = 0, rig_id = 0, joints = 0, clip_count = 0;
    if (bytes.size() < kHeaderBytes || bytes.size() > kMaxBytes) return {};
    word(bytes, 0, magic); word(bytes, 4, version); word(bytes, 8, total); word(bytes, 12, checksum);
    word(bytes, 16, rig_id); word(bytes, 20, joints); word(bytes, 24, clip_count);
    float meters_per_unit = 0.0f;
    if (!detail::floats(bytes, 28, &meters_per_unit, 1) || magic != kMagic || version != 1 ||
        total != bytes.size() || checksum != detail::fnv(bytes, 16) || rig_id == 0 ||
        meters_per_unit != 1.0f ||
        joints < 1 || joints > kMaxJoints || clip_count > kMaxClips) return {};
    const size_t joint_end = kHeaderBytes + size_t(joints) * kJointBytes;
    if (joint_end > bytes.size() || detail::fnv(bytes, kHeaderBytes, joint_end) != rig_id) return {};
    std::vector<float> rest(size_t(joints) * kPoseFloats);
    package.joint_ids.resize(joints);
    package.parents.resize(joints);
    size_t at = kHeaderBytes;
    for (uint32_t joint = 0; joint < joints; ++joint, at += kJointBytes) {
        uint32_t id = 0, parent = 0;
        if (!word(bytes, at, id) || !word(bytes, at + 4, parent) ||
            !detail::floats(bytes, at + 8, rest.data() + size_t(joint) * kPoseFloats, kPoseFloats)) return {};
        const int32_t signed_parent = int32_t(parent);
        if (signed_parent < -1 || signed_parent >= int32_t(joint)) return {};
        if (id == 0 || std::find(package.joint_ids.begin(), package.joint_ids.begin() + joint, id) !=
                package.joint_ids.begin() + joint) return {};
        package.joint_ids[joint] = id;
        package.parents[joint] = signed_parent;
    }
    package.rig_id = rig_id;
    package.rig = build_rig(package.parents.data(), rest.data(), joints);
    if (!package.rig.valid()) return {};
    std::vector<float> key_storage;
    for (uint32_t clip_index = 0; clip_index < clip_count; ++clip_index) {
        uint32_t id = 0, clip_rig = 0, duration_ticks = 0, ticks_per_second = 0, tracks = 0, events = 0;
        if (!word(bytes, at, id) || !word(bytes, at + 4, clip_rig) || !word(bytes, at + 8, duration_ticks) ||
            !word(bytes, at + 12, ticks_per_second) || !word(bytes, at + 16, tracks) ||
            !word(bytes, at + 20, events)) return {};
        if (clip_rig != rig_id || tracks != joints || ticks_per_second == 0 || duration_ticks == 0 ||
            events > kMaxEvents) return {};
        if (id == 0 || std::any_of(package.clips.begin(), package.clips.end(),
                [id](const Clip& clip) { return clip.id == id; })) return {};
        at += kClipHeaderBytes;
        // First pass: bound the clip and size key storage so pointers stay valid.
        size_t scan = at, total_keys = 0;
        for (uint32_t track = 0; track < tracks; ++track) {
            uint32_t joint_index = 0, keys = 0;
            if (!word(bytes, scan, joint_index) || !word(bytes, scan + 4, keys)) return {};
            if (joint_index != track || keys < 1 || total_keys + keys > kMaxKeysPerClip) return {};
            total_keys += keys;
            scan += kTrackHeaderBytes + size_t(keys) * kKeyBytes;
            if (scan > bytes.size()) return {};
        }
        key_storage.assign(total_keys * kPoseFloats, 0.0f);
        const float tps = float(ticks_per_second);
        const float duration = float(duration_ticks) / tps;
        std::vector<std::vector<OzzKey>> track_keys(joints);
        size_t stored = 0;
        for (uint32_t track = 0; track < tracks; ++track) {
            uint32_t keys = 0;
            word(bytes, at + 4, keys);
            at += kTrackHeaderBytes;
            uint32_t previous_tick = 0;
            for (uint32_t key = 0; key < keys; ++key, at += kKeyBytes, ++stored) {
                uint32_t tick = 0;
                float* pose = key_storage.data() + stored * kPoseFloats;
                if (!word(bytes, at, tick) || !detail::floats(bytes, at + 4, pose, kPoseFloats)) return {};
                if (tick > duration_ticks || (key > 0 && tick <= previous_tick) ||
                    !detail::valid_transform(pose)) return {};
                previous_tick = tick;
                track_keys[track].push_back({float(tick) / tps, pose});
            }
        }
        uint32_t previous_event_tick = 0;
        for (uint32_t event = 0; event < events; ++event, at += kEventBytes) {
            uint32_t event_id = 0, tick = 0;
            if (!word(bytes, at, event_id) || !word(bytes, at + 4, tick) || event_id == 0 ||
                tick > duration_ticks || (event > 0 && tick < previous_event_tick)) return {};
            previous_event_tick = tick;
        }
        Clip clip;
        clip.id = id;
        clip.duration_seconds = duration;
        clip.ozz = build_clip(package.rig, duration, track_keys);
        if (!clip.ozz.valid()) return {};
        package.clips.push_back(std::move(clip));
    }
    if (at != bytes.size()) return {};
    return package;
}

} // namespace elisa::animation::anim_v1
