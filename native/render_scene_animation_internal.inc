// Private implementation fragment: internal animation sampling and pose submission.
void clear_animation_state(InstanceSlot& instance) {
    instance.animation_clip = -1;
    instance.previous_animation_clip = -1;
    instance.animation_time = 0.0f;
    instance.previous_animation_time = 0.0f;
    instance.animation_speed = 1.0f;
    instance.previous_animation_speed = 1.0f;
    instance.blend_elapsed = 0.0f;
    instance.blend_duration = 0.0f;
    instance.blend_eased = false;
    instance.animation_loop = true;
    instance.previous_animation_loop = true;
}

int32_t find_animation_clip(const InstanceSlot& instance, const char* requested_name) {
    if (requested_name == nullptr) return -1;
    size_t length = 0;
    while (length <= 1024 && requested_name[length] != '\0') ++length;
    if (length == 0 || length > 1024) return -1;
    const std::string requested(requested_name, length);
    for (size_t index = 0; index < instance.animation_clips.size(); ++index) {
        const std::string& candidate = instance.animation_clips[index].name;
        if (candidate == requested) return int32_t(index);
        const size_t separator = candidate.rfind('|');
        if (separator != std::string::npos && candidate.compare(separator + 1, std::string::npos, requested) == 0) {
            return int32_t(index);
        }
    }
    return -1;
}

float animation_time(const elisa::assets::CookedGeometry::AnimationClip& clip,
    float time, bool loop) {
    if (clip.duration_seconds <= 0.0f) return 0.0f;
    if (!loop) return std::clamp(time, 0.0f, clip.duration_seconds);
    float wrapped = std::fmod(time, clip.duration_seconds);
    if (wrapped < 0.0f) wrapped += clip.duration_seconds;
    return wrapped;
}

void interpolate_transform(const float* first, const float* second, float weight, float* output) {
    for (size_t component = 0; component < 3; ++component) {
        output[component] = first[component] + (second[component] - first[component]) * weight;
    }
    float dot = 0.0f;
    for (size_t component = 0; component < 4; ++component) dot += first[component + 3] * second[component + 3];
    const float direction = dot < 0.0f ? -1.0f : 1.0f;
    float length_squared = 0.0f;
    for (size_t component = 0; component < 4; ++component) {
        const float value = first[component + 3] +
            (second[component + 3] * direction - first[component + 3]) * weight;
        output[component + 3] = value;
        length_squared += value * value;
    }
    if (length_squared > 1.0e-12f && std::isfinite(length_squared)) {
        const float inverse_length = 1.0f / std::sqrt(length_squared);
        for (size_t component = 0; component < 4; ++component) output[component + 3] *= inverse_length;
    } else {
        for (size_t component = 0; component < 4; ++component) output[component + 3] = first[component + 3];
    }
    for (size_t component = 7; component < 10; ++component) {
        output[component] = first[component] + (second[component] - first[component]) * weight;
    }
}

void sample_animation_pose(const InstanceSlot& instance, int32_t clip_index, float time,
    bool loop, size_t joint_index, float* output) {
    if (clip_index < 0) {
        const auto& rest = instance.skin_joints[joint_index].rest_local;
        std::copy(rest.begin(), rest.end(), output);
        return;
    }
    const auto& clip = instance.animation_clips[size_t(clip_index)];
    const float clamped_time = animation_time(clip, time, loop);
    const float frame_position = std::min(clamped_time * float(clip.sample_rate), float(clip.frame_count - 1));
    const uint32_t first_frame = uint32_t(std::floor(frame_position));
    const uint32_t second_frame = std::min(first_frame + 1, clip.frame_count - 1);
    const float weight = frame_position - float(first_frame);
    const size_t joint_count = instance.skin_joints.size();
    const float* first = clip.local_transforms.data() + (size_t(first_frame) * joint_count + joint_index) * 10;
    const float* second = clip.local_transforms.data() + (size_t(second_frame) * joint_count + joint_index) * 10;
    interpolate_transform(first, second, weight, output);
}

float sample_animation_morph_weight(const InstanceSlot& instance, int32_t clip_index,
    float time, bool loop, size_t placement, size_t target, size_t placement_count,
    size_t morph_count) {
    const size_t default_index = placement * morph_count + target;
    if (clip_index < 0 || instance.animation_clips[size_t(clip_index)].morph_weights.empty()) {
        return default_index < instance.morph_default_weights.size()
            ? instance.morph_default_weights[default_index] : 0.0f;
    }
    const auto& clip = instance.animation_clips[size_t(clip_index)];
    const float frame_position = std::min(animation_time(clip, time, loop) * float(clip.sample_rate),
        float(clip.frame_count - 1));
    const uint32_t first_frame = uint32_t(std::floor(frame_position));
    const uint32_t second_frame = std::min(first_frame + 1, clip.frame_count - 1);
    const float amount = frame_position - float(first_frame);
    const size_t first = (size_t(first_frame) * placement_count + placement) * morph_count + target;
    const size_t second = (size_t(second_frame) * placement_count + placement) * morph_count + target;
    if (second >= clip.morph_weights.size()) return 0.0f;
    return clip.morph_weights[first] + (clip.morph_weights[second] - clip.morph_weights[first]) * amount;
}

// Poses are sampled and blended in Elisa space, then reflected into Wicked's
// frame as they are stored, like every other render-service transform.
void store_joint_pose(wi::scene::TransformComponent& transform, const float* pose) {
    transform.translation_local = probe::coordinates::to_wicked(pose[0], pose[1], pose[2]);
    transform.rotation_local = probe::coordinates::to_wicked_rotation(pose[3], pose[4], pose[5], pose[6]);
    transform.scale_local = XMFLOAT3(pose[7], pose[8], pose[9]);
    transform.SetDirty();
}

bool apply_animation_pose(RenderSceneService& state, size_t slot) {
    InstanceSlot& instance = state.instances[slot];
    if (instance.joint_entities.size() != instance.skin_joints.size()) return false;
    const bool has_joints = !instance.skin_joints.empty();
    float blend = 1.0f;
    if (instance.blend_duration > 0.0f) {
        blend = std::clamp(instance.blend_elapsed / instance.blend_duration, 0.0f, 1.0f);
        // Smoothstep removes the velocity jump at both ends of a linear fade.
        if (instance.blend_eased) blend = blend * blend * (3.0f - 2.0f * blend);
    }
    for (size_t joint_index = 0; joint_index < instance.joint_entities.size(); ++joint_index) {
        float current[10]{};
        float previous[10]{};
        float pose[10]{};
        sample_animation_pose(instance, instance.animation_clip, instance.animation_time,
            instance.animation_loop, joint_index, current);
        if (blend < 1.0f) {
            sample_animation_pose(instance, instance.previous_animation_clip, instance.previous_animation_time,
                instance.previous_animation_loop, joint_index, previous);
            interpolate_transform(previous, current, blend, pose);
        } else {
            std::copy(std::begin(current), std::end(current), pose);
        }
        wi::scene::TransformComponent* transform = state.scene->transforms.GetComponent(instance.joint_entities[joint_index]);
        if (transform == nullptr) return false;
        if (instance.skin_joints[joint_index].parent_index < 0) {
            const wi::scene::TransformComponent* parent = state.scene->transforms.GetComponent(instance.entity);
            if (parent == nullptr) return false;
            store_joint_pose(*transform, pose);
            transform->UpdateTransform_Parented(*parent);
        } else {
            const uint32_t parent_index = uint32_t(instance.skin_joints[joint_index].parent_index);
            if (parent_index >= joint_index) return false;
            const wi::scene::TransformComponent* parent = state.scene->transforms.GetComponent(instance.joint_entities[parent_index]);
            if (parent == nullptr) return false;
            store_joint_pose(*transform, pose);
            transform->UpdateTransform_Parented(*parent);
        }
    }
    const auto& children = instance.snapshot_placement_entities.empty()
        ? instance.imported_mesh_entities : instance.snapshot_placement_entities;
    const size_t placement_count = children.size() + 1;
    const wi::scene::MeshComponent* root_mesh = state.scene->meshes.GetComponent(instance.entity);
    if (root_mesh == nullptr) return false;
    const size_t morph_count = root_mesh->morph_targets.size();
    const bool has_morph_tracks = std::any_of(instance.animation_clips.begin(), instance.animation_clips.end(),
        [](const auto& clip) { return !clip.morph_weights.empty(); });
    if (morph_count > 0 && has_morph_tracks) {
        for (size_t placement = 0; placement < placement_count; ++placement) {
            const wi::ecs::Entity entity = placement == 0 ? instance.entity : children[placement - 1];
            wi::scene::MeshComponent* mesh = state.scene->meshes.GetComponent(entity);
            if (mesh == nullptr || mesh->morph_targets.size() != morph_count) return false;
            for (size_t target = 0; target < morph_count; ++target) {
                const float current = sample_animation_morph_weight(instance, instance.animation_clip,
                    instance.animation_time, instance.animation_loop, placement, target, placement_count, morph_count);
                float weight = current;
                if (blend < 1.0f) {
                    const float previous = sample_animation_morph_weight(instance, instance.previous_animation_clip,
                        instance.previous_animation_time, instance.previous_animation_loop, placement, target,
                        placement_count, morph_count);
                    weight = previous + (current - previous) * blend;
                }
                if (!std::isfinite(weight)) return false;
                mesh->morph_targets[target].weight = weight;
            }
        }
    }
    return has_joints || has_morph_tracks;
}
