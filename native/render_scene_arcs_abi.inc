// Dynamic electric ribbons are engine-owned Wicked TrailRenderers. Elisa
// supplies connector endpoints; the backend holds those fixed and flickers
// only the interior samples.
namespace {

constexpr size_t ARC_POINT_COUNT = 9;

void populate_arc_trail(wi::TrailRenderer& trail, const ElectricArcSlot& arc,
    float start_x, float start_y, float start_z,
    float end_x, float end_y, float end_z, float phase, float intensity,
    bool halo) {
    trail.Clear();
    trail.subdivision = 3;
    trail.blendMode = wi::enums::BLENDMODE_ADDITIVE;
    trail.color = XMFLOAT4(1, 1, 1, 1);
    trail.width = 1.0f;
    trail.depth_soften = 0.02f;

    const float dx = end_x - start_x;
    const float dz = end_z - start_z;
    const float planar_length = std::sqrt(dx * dx + dz * dz);
    const float inverse_length = planar_length > 1.0e-5f ? 1.0f / planar_length : 1.0f;
    const float perpendicular_x = planar_length > 1.0e-5f ? -dz * inverse_length : 1.0f;
    const float perpendicular_z = planar_length > 1.0e-5f ? dx * inverse_length : 0.0f;
    const float width = halo ? arc.width * 4.2f : arc.width;
    const XMFLOAT4 tint = halo
        ? XMFLOAT4(0.06f * intensity, 0.36f * intensity, intensity, 0.42f * intensity)
        : XMFLOAT4(0.72f * intensity, 0.91f * intensity, 1.25f * intensity, intensity);

    for (size_t index = 0; index < ARC_POINT_COUNT; ++index) {
        const float t = float(index) / float(ARC_POINT_COUNT - 1);
        float offset = 0.0f;
        float lift = 0.0f;
        if (index != 0 && index + 1 != ARC_POINT_COUNT) {
            const float sample = float(index);
            const float seed = float(arc.seed % 100000u) * 0.001f;
            const float broad = std::sin(phase * 31.0f + seed + sample * 12.9898f);
            const float fine = std::sin(phase * 67.0f + seed * 1.7f + sample * 78.233f);
            const float alternating = (index & 1u) == 0u ? 1.0f : -1.0f;
            offset = arc.amplitude * (0.47f * broad + 0.34f * fine + 0.19f * alternating);
            lift = arc.amplitude * 0.18f * std::sin(phase * 43.0f + seed + sample * 41.17f);
        }
        trail.AddPoint(XMFLOAT3(start_x + dx * t + perpendicular_x * offset,
            start_y + (end_y - start_y) * t + lift,
            start_z + dz * t + perpendicular_z * offset), width, tint);
    }
}

} // namespace

extern "C" int64_t elisa_render_scene_v1_create_electric_arc(
    float width, float amplitude, uint32_t seed) {
    if (!finite(width) || width < 0.0005f || width > 0.25f ||
        !finite(amplitude) || amplitude < 0.0f || amplitude > 0.75f) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    const size_t slot = find_free_arc_slot(state);
    if (slot == MAX_ELECTRIC_ARCS) return ELISA_RENDER_SCENE_CAPACITY;
    ElectricArcSlot& arc = state.electric_arcs[slot];
    if (arc.generation >= MAX_GENERATION) return ELISA_RENDER_SCENE_GENERATION_EXHAUSTED;
    arc.generation += 1;
    arc.seed = seed;
    arc.width = width;
    arc.amplitude = amplitude;
    arc.visible = true;
    arc.live = true;
    arc.halo.Clear();
    arc.core.Clear();
    arc.branch.Clear();
    const uint64_t handle = (arc.generation << HANDLE_SLOT_BITS) | uint64_t(slot + 1);
    return int64_t(handle);
}

extern "C" int32_t elisa_render_scene_v1_update_electric_arc(
    int64_t handle,
    float start_x, float start_y, float start_z,
    float end_x, float end_y, float end_z, float phase, float visibility) {
    if (!bounded(start_x) || !bounded(start_y) || !bounded(start_z) ||
        !bounded(end_x) || !bounded(end_y) || !bounded(end_z) || !bounded(phase, 1.0e6f) ||
        !finite(visibility) || visibility < 0.0f || visibility > 1.0f) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    const double dx = double(end_x) - start_x;
    const double dy = double(end_y) - start_y;
    const double dz = double(end_z) - start_z;
    if (dx * dx + dy * dy + dz * dz <= 1.0e-8) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;

    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    const size_t slot = decode_arc_handle(state, handle);
    if (slot == MAX_ELECTRIC_ARCS) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    ElectricArcSlot& arc = state.electric_arcs[slot];
    const float intensity = visibility * (0.74f + 0.26f * std::abs(std::sin(phase * 39.0f + float(arc.seed % 1000u))));
    populate_arc_trail(arc.halo, arc, start_x, start_y, start_z,
        end_x, end_y, end_z, phase, intensity, true);
    populate_arc_trail(arc.core, arc, start_x, start_y, start_z,
        end_x, end_y, end_z, phase, intensity, false);
    arc.branch.Clear();
    const float branch_gate = std::abs(std::sin(phase * 5.7f + float(arc.seed % 251u)));
    if (branch_gate > 0.985f && visibility > 0.05f) {
        const float across = 0.38f + 0.19f * branch_gate;
        const float branch_x = start_x + float(dx) * across;
        const float branch_y = start_y + (end_y - start_y) * across;
        const float branch_z = start_z + float(dz) * across;
        const float planar_length = std::sqrt(float(dx * dx + dz * dz));
        const float inverse_length = planar_length > 1.0e-5f ? 1.0f / planar_length : 1.0f;
        const float perpendicular_x = planar_length > 1.0e-5f ? -float(dz) * inverse_length : 1.0f;
        const float perpendicular_z = planar_length > 1.0e-5f ? float(dx) * inverse_length : 0.0f;
        const float direction = (arc.seed & 1u) == 0u ? 1.0f : -1.0f;
        arc.branch.subdivision = 2;
        arc.branch.blendMode = wi::enums::BLENDMODE_ADDITIVE;
        arc.branch.color = XMFLOAT4(1, 1, 1, 1);
        arc.branch.width = 1.0f;
        const XMFLOAT4 branch_color(0.65f * intensity, 0.86f * intensity, intensity, intensity);
        arc.branch.AddPoint(XMFLOAT3(branch_x, branch_y, branch_z), arc.width * 0.8f, branch_color);
        arc.branch.AddPoint(XMFLOAT3(branch_x + perpendicular_x * arc.amplitude * 2.2f * direction,
            branch_y + arc.amplitude * 0.6f,
            branch_z + perpendicular_z * arc.amplitude * 2.2f * direction), arc.width * 0.55f, branch_color);
    }
    return ELISA_RENDER_SCENE_OK;
}

extern "C" int32_t elisa_render_scene_v1_set_electric_arc_visible(int64_t handle, int32_t visible) {
    if (visible != 0 && visible != 1) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    const size_t slot = decode_arc_handle(state, handle);
    if (slot == MAX_ELECTRIC_ARCS) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    state.electric_arcs[slot].visible = visible != 0;
    return ELISA_RENDER_SCENE_OK;
}

extern "C" int32_t elisa_render_scene_v1_destroy_electric_arc(int64_t handle) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    const size_t slot = decode_arc_handle(state, handle);
    if (slot == MAX_ELECTRIC_ARCS) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    ElectricArcSlot& arc = state.electric_arcs[slot];
    arc.halo.Clear();
    arc.core.Clear();
    arc.branch.Clear();
    arc.visible = false;
    arc.live = false;
    return ELISA_RENDER_SCENE_OK;
}
