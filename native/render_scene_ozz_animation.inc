// Private implementation fragment: C02 ozz-backed pose sampling for skinned
// instances. A cooked asset's rig and clips are built into one immutable
// OzzAnimationLibrary, shared by every instance (and clone) of that asset.
// Each instance owns its sampling contexts and pose scratch, so instances play
// independent clips and times, and a tick after the first allocates nothing.
struct OzzAnimationLibrary {
    elisa::animation::OzzRig rig;
    std::vector<elisa::animation::OzzClip> clips;  // parallel to the cooked clips
    // Per clip, the full pose at time 0 and at its duration (root cycle deltas).
    std::vector<float> clip_start_poses;
    std::vector<float> clip_end_poses;
    // Keeps the keyed asset alive while the cache refers to it by address.
    std::shared_ptr<const elisa::assets::CookedGeometry> asset;
};

std::shared_ptr<const OzzAnimationLibrary> build_ozz_animation_library(
    const std::vector<elisa::assets::CookedGeometry::SkinJoint>& joints,
    const std::vector<elisa::assets::CookedGeometry::AnimationClip>& clips) {
    namespace anim = elisa::animation;
    auto library = std::make_shared<OzzAnimationLibrary>();
    const size_t joint_count = joints.size();
    if (joint_count == 0) return nullptr;
    std::vector<int32_t> parents(joint_count);
    std::vector<float> rest(joint_count * anim::kPoseFloats);
    for (size_t index = 0; index < joint_count; ++index) {
        parents[index] = joints[index].parent_index;
        std::copy(joints[index].rest_local.begin(), joints[index].rest_local.end(),
            rest.begin() + ptrdiff_t(index * anim::kPoseFloats));
    }
    library->rig = anim::build_rig(parents.data(), rest.data(), joint_count);
    if (!library->rig.valid()) return nullptr;
    const size_t pose_floats = joint_count * anim::kPoseFloats;
    library->clip_start_poses.assign(clips.size() * pose_floats, 0.0f);
    library->clip_end_poses.assign(clips.size() * pose_floats, 0.0f);
    anim::OzzPoseContext context;
    context.prepare(library->rig, int(joint_count));
    for (size_t index = 0; index < clips.size(); ++index) {
        const auto& clip = clips[index];
        const bool sized = clip.frame_count > 0 &&
            clip.local_transforms.size() == size_t(clip.frame_count) * pose_floats;
        library->clips.push_back(sized ? anim::build_fixed_rate_clip(library->rig, clip.local_transforms.data(),
            clip.frame_count, float(clip.sample_rate), clip.duration_seconds) : anim::OzzClip{});
        const anim::OzzClip& built = library->clips.back();
        if (!built.valid()) return nullptr;
        const float end = std::max(clip.duration_seconds, 0.0f);
        if (!context.sample(library->rig, built, 0.0f, library->clip_start_poses.data() + index * pose_floats) ||
            !context.sample(library->rig, built, end, library->clip_end_poses.data() + index * pose_floats)) {
            return nullptr;
        }
    }
    return library;
}

// Libraries by cooked asset address. Every caller holds the service mutex.
std::shared_ptr<const OzzAnimationLibrary> ozz_library_for_asset(
    const std::shared_ptr<const elisa::assets::CookedGeometry>& asset,
    const std::vector<elisa::assets::CookedGeometry::SkinJoint>& joints,
    const std::vector<elisa::assets::CookedGeometry::AnimationClip>& clips) {
    static std::unordered_map<const void*, std::weak_ptr<const OzzAnimationLibrary>> cache;
    if (asset == nullptr) return build_ozz_animation_library(joints, clips);
    for (auto entry = cache.begin(); entry != cache.end();) {
        entry = entry->second.expired() ? cache.erase(entry) : std::next(entry);
    }
    if (auto found = cache.find(asset.get()); found != cache.end()) {
        if (auto library = found->second.lock()) return library;
    }
    auto built = build_ozz_animation_library(joints, clips);
    if (built == nullptr) return nullptr;
    std::const_pointer_cast<OzzAnimationLibrary>(built)->asset = asset;
    cache[asset.get()] = built;
    return built;
}

void reset_ozz_animation(InstanceSlot& instance) {
    instance.ozz_library.reset();
    instance.ozz_current = {};
    instance.ozz_previous = {};
    instance.ozz_current_pose.clear();
    instance.ozz_previous_pose.clear();
}

// Binds the instance's library and prepares its contexts and scratch. Runs
// allocation work only on the first call after a mesh or clip set changes.
bool ensure_ozz_animation(InstanceSlot& instance) {
    const size_t joint_count = instance.skin_joints.size();
    const auto& library = instance.ozz_library;
    if (library == nullptr || library->rig.joint_count() != joint_count ||
        library->clips.size() != instance.animation_clips.size()) {
        instance.ozz_library = ozz_library_for_asset(instance.animated_asset,
            instance.skin_joints, instance.animation_clips);
        if (instance.ozz_library == nullptr) return false;
    }
    instance.ozz_current.ensure(instance.ozz_library->rig);
    instance.ozz_previous.ensure(instance.ozz_library->rig);
    instance.ozz_current_pose.resize(joint_count * elisa::animation::kPoseFloats);
    instance.ozz_previous_pose.resize(joint_count * elisa::animation::kPoseFloats);
    return true;
}

float animation_time(const elisa::assets::CookedGeometry::AnimationClip& clip, float time, bool loop);

// Samples every joint of `clip_index` (rest pose when negative) into `output`.
bool sample_ozz_full_pose(InstanceSlot& instance, elisa::animation::OzzContextSlot& slot,
    int32_t clip_index, float time, bool loop, float* output) {
    const OzzAnimationLibrary& library = *instance.ozz_library;
    if (clip_index < 0) {
        std::copy(library.rig.rest.begin(), library.rig.rest.end(), output);
        return true;
    }
    if (size_t(clip_index) >= library.clips.size()) return false;
    const auto& clip = instance.animation_clips[size_t(clip_index)];
    return slot.ensure(library.rig).sample(library.rig, library.clips[size_t(clip_index)],
        animation_time(clip, time, loop), output);
}
