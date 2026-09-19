#pragma once
// The specialist-library checks the native host runs, in one place. Each check
// keeps its own header; this sequence keeps the probe's main flow short and
// makes the exercised-library order explicit. The order matters only where a
// check depends on scene state built earlier (the perf sweep and churn run
// against the live scene), so the scene-taking checks run first.
#include "probe_support.h"
#include "perf_sweep.h"
#include "churn_probe.h"
#include "ozz_probe.h"
#include "recast_probe.h"
#include "miniaudio_probe.h"
#include "text_probe.h"
#include "udp_probe.h"
#include "menu_probe.h"
#include "pose_probe.h"
#include "input_probe.h"
#include "skin_probe.h"
#include "layout_probe.h"
#include "coordinate_probe.h"

#include <map>
#include <string>

namespace probe {

inline bool run_library_probes(wi::Application& application, wi::scene::Scene& scene,
    const std::map<std::string, std::string>& manifest) {
    if (!run_perf_sweep(application, scene, manifest)) {
        return false;
    }
    if (!run_churn_probe(scene, manifest)) {
        return false;
    }
    if (!probe_ozz_sampling()) {
        return false;
    }
    if (!probe_recast_navigation()) {
        return false;
    }
    if (!probe_miniaudio()) {
        return false;
    }
    if (!probe_text()) {
        return false;
    }
    if (!probe_udp_loopback()) {
        return false;
    }
    if (!probe_menu(manifest)) {
        return false;
    }
    if (!probe_pose(manifest)) {
        return false;
    }
    if (!probe_input(manifest)) {
        return false;
    }
    if (!probe_skinned_quad(scene, manifest)) {
        return false;
    }
    if (!run_layout_probe(scene)) {
        return false;
    }
    if (!probe_coordinate_conventions()) {
        return false;
    }
    return true;
}

} // namespace probe
