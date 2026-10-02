#pragma once
// C02 ozz runtime animation service. Promotes the ozz link probe into three
// reusable pieces with explicit storage:
//
//   OzzRig          immutable ozz skeleton plus engine<->ozz joint index maps
//                   and the rest pose; built once per cooked asset.
//   OzzClip         immutable ozz animation built from either the cooker's
//                   fixed-rate tracks or keyed elisa-anim-v1 tracks.
//   OzzPoseContext  per character: the ozz sampling cache and SoA scratch,
//                   prepared once for a rig. sample() writes a full engine
//                   pose (10 floats per joint: T xyz, R xyzw, S xyz) into
//                   caller-owned output and performs no heap allocation.
//
// Rigs and clips are shared between any number of characters; each character
// owns its contexts, so independent clips and times never contend. ozz stores
// translation and scale keys as half floats and rotations as 3x16-bit
// quantized quaternions, so sampled values differ from the float source by the
// key quantization (about 5e-4 relative for translation/scale).
#include "ozz/animation/offline/animation_builder.h"
#include "ozz/animation/offline/raw_animation.h"
#include "ozz/animation/offline/raw_skeleton.h"
#include "ozz/animation/offline/skeleton_builder.h"
#include "ozz/animation/runtime/animation.h"
#include "ozz/animation/runtime/sampling_job.h"
#include "ozz/animation/runtime/skeleton.h"
#include "ozz/base/maths/simd_math.h"
#include "ozz/base/maths/soa_transform.h"
#include "ozz/base/span.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace elisa::animation {

constexpr size_t kPoseFloats = 10;

struct OzzRig {
    ozz::unique_ptr<ozz::animation::Skeleton> skeleton;
    std::vector<int32_t> engine_to_ozz;
    std::vector<int32_t> ozz_to_engine;
    std::vector<float> rest;  // kPoseFloats per engine joint
    size_t joint_count() const { return engine_to_ozz.size(); }
    bool valid() const { return skeleton != nullptr && !engine_to_ozz.empty(); }
};

struct OzzClip {
    ozz::unique_ptr<ozz::animation::Animation> animation;
    float duration_seconds = 0.0f;  // ozz duration; ratio = seconds / this
    bool valid() const { return animation != nullptr && duration_seconds > 0.0f; }
};

namespace detail {
inline ozz::math::Transform to_ozz(const float* pose) {
    ozz::math::Transform transform;
    transform.translation = ozz::math::Float3(pose[0], pose[1], pose[2]);
    transform.rotation = ozz::math::Quaternion(pose[3], pose[4], pose[5], pose[6]);
    transform.scale = ozz::math::Float3(pose[7], pose[8], pose[9]);
    return transform;
}

inline bool finite_pose(const float* pose) {
    for (size_t index = 0; index < kPoseFloats; ++index) {
        if (!std::isfinite(pose[index])) return false;
    }
    return true;
}

inline void add_children(ozz::animation::offline::RawSkeleton::Joint& joint, int32_t engine_index,
    const std::vector<std::vector<int32_t>>& children, const float* rest) {
    joint.name = "j" + std::to_string(engine_index);
    joint.transform = to_ozz(rest + size_t(engine_index) * kPoseFloats);
    const auto& list = children[size_t(engine_index)];
    joint.children.resize(list.size());
    for (size_t slot = 0; slot < list.size(); ++slot) {
        add_children(joint.children[slot], list[slot], children, rest);
    }
}
} // namespace detail

// Builds a rig from parent indices (parents precede children, -1 = root) and
// the engine rest pose. Returns an invalid rig on malformed input.
inline OzzRig build_rig(const int32_t* parents, const float* rest, size_t joint_count) {
    OzzRig rig;
    if (joint_count == 0 || joint_count > size_t(ozz::animation::Skeleton::kMaxJoints)) return rig;
    std::vector<std::vector<int32_t>> children(joint_count);
    ozz::animation::offline::RawSkeleton raw;
    for (size_t index = 0; index < joint_count; ++index) {
        if (!detail::finite_pose(rest + index * kPoseFloats)) return rig;
        const int32_t parent = parents[index];
        if (parent >= int32_t(index) || parent < -1) return rig;
        if (parent >= 0) children[size_t(parent)].push_back(int32_t(index));
    }
    for (size_t index = 0; index < joint_count; ++index) {
        if (parents[index] >= 0) continue;
        raw.roots.emplace_back();
        detail::add_children(raw.roots.back(), int32_t(index), children, rest);
    }
    if (!raw.Validate()) return rig;
    auto skeleton = ozz::animation::offline::SkeletonBuilder()(raw);
    if (!skeleton || size_t(skeleton->num_joints()) != joint_count) return rig;
    rig.engine_to_ozz.assign(joint_count, -1);
    rig.ozz_to_engine.assign(joint_count, -1);
    const auto names = skeleton->joint_names();
    for (size_t ozz_index = 0; ozz_index < joint_count; ++ozz_index) {
        const char* name = names[ozz_index];
        if (name == nullptr || name[0] != 'j') return OzzRig{};
        const long engine_index = std::strtol(name + 1, nullptr, 10);
        if (engine_index < 0 || size_t(engine_index) >= joint_count ||
            rig.engine_to_ozz[size_t(engine_index)] != -1) return OzzRig{};
        rig.engine_to_ozz[size_t(engine_index)] = int32_t(ozz_index);
        rig.ozz_to_engine[ozz_index] = int32_t(engine_index);
    }
    rig.rest.assign(rest, rest + joint_count * kPoseFloats);
    rig.skeleton = std::move(skeleton);
    return rig;
}

// One keyed engine-space transform at `seconds`.
struct OzzKey {
    float seconds;
    const float* pose;  // kPoseFloats
};

inline OzzClip build_clip(const OzzRig& rig, float duration_seconds,
    const std::vector<std::vector<OzzKey>>& tracks) {
    OzzClip clip;
    if (!rig.valid() || tracks.size() != rig.joint_count() ||
        !(duration_seconds > 0.0f) || !std::isfinite(duration_seconds)) return clip;
    ozz::animation::offline::RawAnimation raw;
    raw.duration = duration_seconds;
    raw.tracks.resize(rig.joint_count());
    for (size_t engine = 0; engine < tracks.size(); ++engine) {
        auto& track = raw.tracks[size_t(rig.engine_to_ozz[engine])];
        for (const OzzKey& key : tracks[engine]) {
            if (!(key.seconds >= 0.0f) || key.seconds > duration_seconds ||
                !detail::finite_pose(key.pose)) return clip;
            const float* pose = key.pose;
            track.translations.push_back({key.seconds, ozz::math::Float3(pose[0], pose[1], pose[2])});
            track.rotations.push_back({key.seconds, ozz::math::Quaternion(pose[3], pose[4], pose[5], pose[6])});
            track.scales.push_back({key.seconds, ozz::math::Float3(pose[7], pose[8], pose[9])});
        }
    }
    if (!raw.Validate()) return clip;
    clip.animation = ozz::animation::offline::AnimationBuilder()(raw);
    clip.duration_seconds = clip.animation ? duration_seconds : 0.0f;
    return clip;
}

// The cooker's fixed-rate layout: frame-major, kPoseFloats per joint. The
// engine holds the last frame past (frame_count - 1) / rate, so the ozz
// duration is at least that span and keys sit at frame / rate.
inline OzzClip build_fixed_rate_clip(const OzzRig& rig, const float* frames, uint32_t frame_count,
    float sample_rate, float duration_seconds) {
    if (!rig.valid() || frames == nullptr || frame_count == 0 || !(sample_rate > 0.0f)) return OzzClip{};
    const float span = float(frame_count - 1) / sample_rate;
    float duration = std::max(duration_seconds, span);
    if (!(duration > 0.0f)) duration = 1.0f / sample_rate;
    const size_t joints = rig.joint_count();
    std::vector<std::vector<OzzKey>> tracks(joints);
    for (size_t joint = 0; joint < joints; ++joint) {
        tracks[joint].reserve(frame_count);
        for (uint32_t frame = 0; frame < frame_count; ++frame) {
            const float seconds = std::min(float(frame) / sample_rate, duration);
            tracks[joint].push_back({seconds, frames + (size_t(frame) * joints + joint) * kPoseFloats});
        }
    }
    return build_clip(rig, duration, tracks);
}

class OzzPoseContext {
public:
    // Allocates the sampling cache and scratch for `rig` (load time only).
    void prepare(const OzzRig& rig, int max_tracks) {
        const int tracks = std::max(max_tracks, rig.valid() ? rig.skeleton->num_joints() : 0);
        if (!context_ || capacity_ < tracks) {
            context_ = std::make_unique<ozz::animation::SamplingJob::Context>(tracks);
            capacity_ = tracks;
        }
        locals_.resize(rig.valid() ? size_t(rig.skeleton->num_soa_joints()) : 0);
    }

    bool prepared_for(const OzzRig& rig) const {
        return context_ && rig.valid() && locals_.size() == size_t(rig.skeleton->num_soa_joints()) &&
            capacity_ >= rig.skeleton->num_joints();
    }

    // Samples `clip` at `seconds` (already looped/clamped by the caller) into
    // `output` (kPoseFloats * rig.joint_count(), engine joint order). No heap
    // allocation happens here once prepare() ran for this rig.
    bool sample(const OzzRig& rig, const OzzClip& clip, float seconds, float* output) {
        if (!prepared_for(rig) || !clip.valid() || output == nullptr ||
            clip.animation->num_tracks() != rig.skeleton->num_joints() ||
            clip.animation->num_tracks() > capacity_) return false;
        ozz::animation::SamplingJob job;
        job.animation = clip.animation.get();
        job.context = context_.get();
        job.ratio = std::clamp(seconds / clip.duration_seconds, 0.0f, 1.0f);
        job.output = ozz::make_span(locals_);
        if (!job.Run()) return false;
        const size_t joints = rig.joint_count();
        for (size_t soa = 0; soa < locals_.size(); ++soa) {
            const ozz::math::SoaTransform& value = locals_[soa];
            float lanes[kPoseFloats][4];
            ozz::math::StorePtrU(value.translation.x, lanes[0]);
            ozz::math::StorePtrU(value.translation.y, lanes[1]);
            ozz::math::StorePtrU(value.translation.z, lanes[2]);
            ozz::math::StorePtrU(value.rotation.x, lanes[3]);
            ozz::math::StorePtrU(value.rotation.y, lanes[4]);
            ozz::math::StorePtrU(value.rotation.z, lanes[5]);
            ozz::math::StorePtrU(value.rotation.w, lanes[6]);
            ozz::math::StorePtrU(value.scale.x, lanes[7]);
            ozz::math::StorePtrU(value.scale.y, lanes[8]);
            ozz::math::StorePtrU(value.scale.z, lanes[9]);
            for (size_t lane = 0; lane < 4; ++lane) {
                const size_t ozz_index = soa * 4 + lane;
                if (ozz_index >= joints) break;
                float* out = output + size_t(rig.ozz_to_engine[ozz_index]) * kPoseFloats;
                for (size_t component = 0; component < kPoseFloats; ++component) {
                    out[component] = lanes[component][lane];
                }
            }
        }
        return true;
    }

private:
    std::unique_ptr<ozz::animation::SamplingJob::Context> context_;
    int capacity_ = 0;
    std::vector<ozz::math::SoaTransform> locals_;
};

// Copyable holder for per-instance contexts: a copy starts unprepared, so a
// cloned character never shares sampling cache state with its source.
struct OzzContextSlot {
    std::unique_ptr<OzzPoseContext> context;
    OzzContextSlot() = default;
    OzzContextSlot(const OzzContextSlot&) {}
    OzzContextSlot& operator=(const OzzContextSlot&) { context.reset(); return *this; }
    OzzContextSlot(OzzContextSlot&&) noexcept = default;
    OzzContextSlot& operator=(OzzContextSlot&&) noexcept = default;
    OzzPoseContext& ensure(const OzzRig& rig) {
        if (!context) context = std::make_unique<OzzPoseContext>();
        if (!context->prepared_for(rig)) context->prepare(rig, rig.valid() ? rig.skeleton->num_joints() : 0);
        return *context;
    }
};

} // namespace elisa::animation
