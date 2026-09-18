#pragma once
// Native consumption of the character leg pose. The joints are solved by the
// engine's IK and carried as cell-unit coordinates; the native host parses and
// validates them, matching the Godot host building joint markers from the same
// numbers. This is data-level consumption, not a skinned mesh submission.
#include "probe_core.h"

#include <cstdio>
#include <map>
#include <string>
#include <utility>

namespace probe {

inline std::pair<float, float> parse_pair(const std::string& spec) {
    const auto comma = spec.find(',');
    if (comma == std::string::npos) {
        return {0.0f, 0.0f};
    }
    return {std::stof(spec.substr(0, comma)), std::stof(spec.substr(comma + 1))};
}

inline bool probe_pose(const std::map<std::string, std::string>& manifest) {
    const auto hip_it = manifest.find("pose_hip");
    const auto knee_it = manifest.find("pose_knee");
    const auto foot_it = manifest.find("pose_foot");
    if (hip_it == manifest.end() || knee_it == manifest.end() || foot_it == manifest.end()) {
        return true;
    }
    const auto hip = parse_pair(hip_it->second);
    const auto knee = parse_pair(knee_it->second);
    const auto foot = parse_pair(foot_it->second);
    if (!check(hip.second > knee.second && knee.second > foot.second,
            "pose knee is between the hip and the foot")) {
        return false;
    }
    std::fprintf(stdout, "pose: hip_y=%.3f knee_y=%.3f foot_y=%.3f\n",
        hip.second, knee.second, foot.second);
    return true;
}

} // namespace probe
