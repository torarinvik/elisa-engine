#pragma once

extern "C" int32_t elisa_render_scene_v1_test_arc_depth_matches(int32_t enabled) {
    if (enabled != 0 && enabled != 1) return 0;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state)) return 0;
    const size_t slot = decode_arc_handle(state, state.arc_depth_test_probe_handle);
    if (slot == MAX_ELECTRIC_ARCS) return 0;
    const ElectricArcSlot& arc = state.electric_arcs[slot];
    const bool expected = enabled != 0;
    const float expected_soften = expected ? 0.0f : ArcTuning::TRAIL_DEPTH_SOFTEN;
    return arc.depth_test == expected &&
        arc.halo.depth_test_enabled == expected &&
        arc.core.depth_test_enabled == expected &&
        arc.branch.depth_test_enabled == expected &&
        std::fabs(arc.halo.depth_soften - expected_soften) < 0.0001f &&
        std::fabs(arc.core.depth_soften - expected_soften) < 0.0001f &&
        std::fabs(arc.branch.depth_soften - expected_soften) < 0.0001f ? 1 : 0;
}
