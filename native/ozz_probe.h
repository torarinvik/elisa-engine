#pragma once
// ozz-animation integration. The plan selects ozz for skeletal animation; here
// the runtime is linked and sampling is checked rather than merely declared. A
// two-joint skeleton and a linear translation clip are built in code, sampled
// at start, midpoint, and end, and compared against the same linear expectation
// the engine's own sampler produces. This proves ozz is linked and sampling; it
// does not claim a full character pipeline was ported to ozz.
#include "probe_core.h"

#include "ozz/animation/offline/animation_builder.h"
#include "ozz/animation/offline/raw_animation.h"
#include "ozz/animation/offline/raw_skeleton.h"
#include "ozz/animation/offline/skeleton_builder.h"
#include "ozz/animation/runtime/animation.h"
#include "ozz/animation/runtime/local_to_model_job.h"
#include "ozz/animation/runtime/sampling_job.h"
#include "ozz/animation/runtime/skeleton.h"
#include "ozz/base/maths/simd_math.h"
#include "ozz/base/maths/soa_transform.h"
#include "ozz/base/span.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace probe {

inline float sample_root_translation_x(float ratio) {
    ozz::animation::offline::RawSkeleton raw_skeleton;
    raw_skeleton.roots.resize(1);
    auto& root = raw_skeleton.roots[0];
    root.name = "root";
    root.transform = ozz::math::Transform::identity();
    root.children.resize(1);
    auto& child = root.children[0];
    child.name = "child";
    child.transform.translation = ozz::math::Float3(1.f, 0.f, 0.f);
    if (!check(raw_skeleton.Validate(), "ozz raw skeleton valid")) {
        return 0.f;
    }
    ozz::unique_ptr<ozz::animation::Skeleton> skeleton =
        ozz::animation::offline::SkeletonBuilder()(raw_skeleton);
    if (!check(skeleton != nullptr, "ozz skeleton build")) {
        return 0.f;
    }
    ozz::animation::offline::RawAnimation raw_animation;
    raw_animation.duration = 1.f;
    raw_animation.tracks.resize(skeleton->num_joints());
    raw_animation.tracks[0].translations.push_back({0.f, ozz::math::Float3(0.f, 0.f, 0.f)});
    raw_animation.tracks[0].translations.push_back({1.f, ozz::math::Float3(2.f, 0.f, 0.f)});
    if (!check(raw_animation.Validate(), "ozz raw animation valid")) {
        return 0.f;
    }
    ozz::unique_ptr<ozz::animation::Animation> animation =
        ozz::animation::offline::AnimationBuilder()(raw_animation);
    if (!check(animation != nullptr, "ozz animation build")) {
        return 0.f;
    }
    std::vector<ozz::math::SoaTransform> locals(skeleton->num_soa_joints());
    ozz::animation::SamplingJob::Context context(animation->num_tracks());
    ozz::animation::SamplingJob sampling;
    sampling.animation = animation.get();
    sampling.context = &context;
    sampling.ratio = ratio;
    sampling.output = ozz::make_span(locals);
    if (!check(sampling.Run(), "ozz sampling run")) {
        return 0.f;
    }
    std::vector<ozz::math::Float4x4> models(skeleton->num_joints());
    ozz::animation::LocalToModelJob local_to_model;
    local_to_model.skeleton = skeleton.get();
    local_to_model.input = ozz::make_span(locals);
    local_to_model.output = ozz::make_span(models);
    if (!check(local_to_model.Run(), "ozz local-to-model run")) {
        return 0.f;
    }
    return models[0].cols[3].x;
}

inline bool probe_ozz_sampling() {
    const float start = sample_root_translation_x(0.f);
    const float midpoint = sample_root_translation_x(0.5f);
    const float finish = sample_root_translation_x(1.f);
    std::fprintf(stdout, "ozz: root_x start=%.4f mid=%.4f end=%.4f\n", start, midpoint, finish);
    return check(std::fabs(start - 0.f) < 0.001f &&
        std::fabs(midpoint - 1.f) < 0.001f &&
        std::fabs(finish - 2.f) < 0.001f,
        "ozz samples the linear clip at the expected values");
}

} // namespace probe
