#pragma once

// Parser for bounded joint and morph-weight animation samples.

namespace elisa::assets::detail {

inline bool parse_geometry_animations(const probe::PackageIndex& package,
    CookedGeometry& geometry, std::string& error) {
    const auto count_section = package.sections.find("animation_clips");
    const auto contract_format = package.sections.find("animation_contract_format");
    const auto contract_size = package.sections.find("animation_contract_bytes");
    const auto contract_data = package.sections.find("animation_contract_b64");
    const size_t contract_fields = size_t(contract_format != package.sections.end()) +
        size_t(contract_size != package.sections.end()) + size_t(contract_data != package.sections.end());
    if (contract_fields != 0 && (contract_fields != 3 || count_section == package.sections.end())) {
        error = "incomplete cooked animation contract metadata";
        return false;
    }
    if (count_section == package.sections.end()) return true;
    uint64_t clip_count = 0;
    if (!parse_count(package, "animation_clips", clip_count) ||
        clip_count > MAX_GEOMETRY_ANIMATION_CLIPS) {
        error = "invalid cooked geometry animation clip count";
        return false;
    }
    if (contract_fields != 0) {
        uint64_t expected_bytes = 0;
        if (contract_format->second != "elisa-anim-v1" || geometry.skin_joints.empty() ||
            clip_count == 0 || !parse_count(package, "animation_contract_bytes", expected_bytes) ||
            expected_bytes == 0 || expected_bytes > 64u * 1024u * 1024u ||
            !decode_base64(contract_data->second, geometry.animation_contract) ||
            geometry.animation_contract.size() != expected_bytes) {
            error = "invalid cooked animation contract metadata or bytes";
            return false;
        }
    }
    const size_t joint_count = geometry.skin_joints.size();
    const size_t morph_count = geometry.morph_targets.size();
    const size_t placement_count = geometry.mesh_placements.empty() ? 1 : geometry.mesh_placements.size();
    size_t total_sample_floats = 0;
    for (size_t clip_index = 0; clip_index < size_t(clip_count); ++clip_index) {
        const std::string prefix = "animation_" + std::to_string(clip_index) + "_";
        const auto encoded_name = package.sections.find(prefix + "name_b64");
        const auto sample_rate = package.sections.find(prefix + "sample_rate");
        const auto frames_section = package.sections.find(prefix + "frames");
        const auto transform_stride = package.sections.find(prefix + "transform_stride");
        const auto transform_samples = package.sections.find(prefix + "samples_b64");
        const auto morph_stride = package.sections.find(prefix + "morph_stride");
        const auto morph_samples = package.sections.find(prefix + "morph_samples_b64");
        const bool has_transforms = transform_stride != package.sections.end() ||
            transform_samples != package.sections.end();
        const bool has_morphs = morph_stride != package.sections.end() ||
            morph_samples != package.sections.end();
        float duration = 0.0f;
        uint64_t rate = 0;
        uint64_t frames = 0;
        if (encoded_name == package.sections.end() || sample_rate == package.sections.end() ||
            frames_section == package.sections.end() ||
            !parse_finite_float(package, prefix + "duration_seconds", duration) || duration <= 0.0f ||
            !parse_count(package, (prefix + "sample_rate").c_str(), rate) || rate == 0 || rate > 120 ||
            !parse_count(package, (prefix + "frames").c_str(), frames) || frames < 2 || frames > 3601 ||
            has_transforms != (joint_count > 0) || (has_transforms &&
                (transform_stride == package.sections.end() || transform_stride->second != "40" ||
                    transform_samples == package.sections.end())) ||
            (has_morphs && (morph_count == 0 || morph_stride == package.sections.end() ||
                morph_stride->second != "4" ||
                morph_samples == package.sections.end()))) {
            error = "invalid cooked geometry animation metadata";
            return false;
        }
        const size_t transforms_per_frame = joint_count * 10;
        const size_t morphs_per_frame = has_morphs ? placement_count * morph_count : 0;
        if (transforms_per_frame > MAX_GEOMETRY_ANIMATION_SAMPLE_FLOATS || morphs_per_frame > MAX_GEOMETRY_ANIMATION_SAMPLE_FLOATS - transforms_per_frame) {
            error = "cooked geometry animation samples exceed the runtime bound";
            return false;
        }
        const size_t sample_floats_per_frame = transforms_per_frame + morphs_per_frame;
        if (sample_floats_per_frame == 0 || size_t(frames) >
                (MAX_GEOMETRY_ANIMATION_SAMPLE_FLOATS - total_sample_floats) / sample_floats_per_frame) {
            error = "cooked geometry animation samples exceed the runtime bound";
            return false;
        }
        CookedGeometry::AnimationClip clip;
        std::vector<uint8_t> name_bytes;
        std::vector<std::string> names;
        if (!decode_base64(encoded_name->second, name_bytes) || !decode_names(name_bytes, 1, names) ||
            names[0].empty() || (has_transforms && !decode_floats(package,
                (prefix + "samples_b64").c_str(), size_t(frames) * transforms_per_frame,
                clip.local_transforms)) || (has_morphs && !decode_floats(package,
                (prefix + "morph_samples_b64").c_str(), size_t(frames) * morphs_per_frame,
                clip.morph_weights))) {
            error = "invalid cooked geometry animation samples or name";
            return false;
        }
        clip.name = std::move(names[0]);
        clip.duration_seconds = duration;
        clip.sample_rate = uint32_t(rate);
        clip.frame_count = uint32_t(frames);
        for (size_t offset = 0; offset < clip.local_transforms.size(); offset += 10) {
            const float* transform = clip.local_transforms.data() + offset;
            const float rotation_length = std::sqrt(transform[3] * transform[3] + transform[4] * transform[4] +
                transform[5] * transform[5] + transform[6] * transform[6]);
            if (!(rotation_length > 0.99f && rotation_length < 1.01f) ||
                transform[7] == 0.0f || transform[8] == 0.0f || transform[9] == 0.0f) {
                error = "cooked geometry animation has an invalid transform sample";
                return false;
            }
        }
        total_sample_floats += size_t(frames) * sample_floats_per_frame;
        geometry.animation_clips.push_back(std::move(clip));
    }
    return true;
}

} // namespace elisa::assets::detail
