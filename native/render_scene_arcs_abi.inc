// Dynamic electric ribbons are engine-owned Wicked TrailRenderers. Elisa
// supplies connector endpoints; the backend holds those fixed and flickers
// only the interior samples.
namespace {

size_t decode_arc_handle(const RenderSceneService& state, int64_t handle) {
    if (handle <= 0) return MAX_ELECTRIC_ARCS;
    const uint64_t value = uint64_t(handle);
    const uint64_t encoded_slot = value & HANDLE_SLOT_MASK;
    if (encoded_slot == 0 || encoded_slot > MAX_ELECTRIC_ARCS) return MAX_ELECTRIC_ARCS;
    const size_t slot = size_t(encoded_slot - 1);
    const uint64_t generation = value >> HANDLE_SLOT_BITS;
    const ElectricArcSlot& arc = state.electric_arcs[slot];
    return generation != 0 && arc.live && arc.generation == generation ? slot : MAX_ELECTRIC_ARCS;
}

namespace ArcTuning {

constexpr size_t POINT_COUNT = 9;
constexpr uint32_t SHAPE_SEED_MODULUS = 100000;
constexpr uint32_t INTENSITY_SEED_MODULUS = 1000;
constexpr uint32_t BRANCH_SEED_MODULUS = 251;
constexpr float MIN_WIDTH = 0.0005f;
constexpr float MAX_WIDTH = 0.25f;
constexpr float MAX_AMPLITUDE = 0.75f;
constexpr float MAX_PHASE = 1.0e6f;
constexpr float MIN_PLANAR_LENGTH = 1.0e-5f;
constexpr double MIN_SEGMENT_LENGTH_SQUARED = 1.0e-8;
constexpr uint32_t TRAIL_SUBDIVISION = 3;
constexpr uint32_t BRANCH_SUBDIVISION = 2;
constexpr float TRAIL_WIDTH = 1.0f;
constexpr float TRAIL_DEPTH_SOFTEN = 0.02f;
constexpr float HALO_WIDTH_SCALE = 4.2f;
constexpr float HALO_RED = 0.06f;
constexpr float HALO_GREEN = 0.36f;
constexpr float HALO_ALPHA = 0.42f;
constexpr float CORE_RED = 0.72f;
constexpr float CORE_GREEN = 0.91f;
constexpr float CORE_BLUE = 1.25f;
constexpr float INTENSITY_BASE = 0.74f;
constexpr float INTENSITY_VARIANCE = 0.26f;
constexpr float INTENSITY_FREQUENCY = 39.0f;
constexpr float SEED_PHASE_SCALE = 0.001f;
constexpr float SHAPE_SEED_SCALE = 1.7f;
constexpr float BROAD_FREQUENCY = 31.0f;
constexpr float BROAD_SAMPLE_SPACING = 12.9898f;
constexpr float FINE_FREQUENCY = 67.0f;
constexpr float FINE_SAMPLE_SPACING = 78.233f;
constexpr float LIFT_FREQUENCY = 43.0f;
constexpr float LIFT_SAMPLE_SPACING = 41.17f;
constexpr float BROAD_OFFSET_WEIGHT = 0.47f;
constexpr float FINE_OFFSET_WEIGHT = 0.34f;
constexpr float ALTERNATING_OFFSET_WEIGHT = 0.19f;
constexpr float LIFT_SCALE = 0.18f;
constexpr float BRANCH_FREQUENCY = 5.7f;
constexpr float BRANCH_GATE_THRESHOLD = 0.985f;
constexpr float MIN_BRANCH_VISIBILITY = 0.05f;
constexpr float BRANCH_START = 0.38f;
constexpr float BRANCH_SPAN = 0.19f;
constexpr float BRANCH_WIDTH_SCALE = 0.8f;
constexpr float BRANCH_TIP_WIDTH_SCALE = 0.55f;
constexpr float BRANCH_LENGTH_SCALE = 2.2f;
constexpr float BRANCH_LIFT_SCALE = 0.6f;
constexpr float BRANCH_RED = 0.65f;
constexpr float BRANCH_GREEN = 0.86f;

} // namespace ArcTuning

void populate_arc_trail(wi::TrailRenderer& trail, const ElectricArcSlot& arc,
    float start_x, float start_y, float start_z,
    float end_x, float end_y, float end_z, float phase, float intensity,
    bool halo) {
    trail.Clear();
    trail.subdivision = ArcTuning::TRAIL_SUBDIVISION;
    trail.blendMode = wi::enums::BLENDMODE_ADDITIVE;
    trail.depth_test_enabled = arc.depth_test;
    trail.color = XMFLOAT4(1, 1, 1, 1);
    trail.width = ArcTuning::TRAIL_WIDTH;
    // Orthographic arcs use hardware depth only; the legacy soft fade assumes perspective.
    trail.depth_soften = arc.depth_test ? 0.0f : ArcTuning::TRAIL_DEPTH_SOFTEN;

    const float dx = end_x - start_x;
    const float dz = end_z - start_z;
    const float planar_length = std::sqrt(dx * dx + dz * dz);
    const float inverse_length = planar_length > ArcTuning::MIN_PLANAR_LENGTH ? 1.0f / planar_length : 1.0f;
    const float perpendicular_x = planar_length > ArcTuning::MIN_PLANAR_LENGTH ? -dz * inverse_length : 1.0f;
    const float perpendicular_z = planar_length > ArcTuning::MIN_PLANAR_LENGTH ? dx * inverse_length : 0.0f;
    const float width = halo ? arc.width * ArcTuning::HALO_WIDTH_SCALE : arc.width;
    const XMFLOAT4 tint = halo
        ? XMFLOAT4(ArcTuning::HALO_RED * intensity, ArcTuning::HALO_GREEN * intensity,
            intensity, ArcTuning::HALO_ALPHA * intensity)
        : XMFLOAT4(ArcTuning::CORE_RED * intensity, ArcTuning::CORE_GREEN * intensity,
            ArcTuning::CORE_BLUE * intensity, intensity);

    for (size_t index = 0; index < ArcTuning::POINT_COUNT; ++index) {
        const float t = float(index) / float(ArcTuning::POINT_COUNT - 1);
        float offset = 0.0f;
        float lift = 0.0f;
        if (index != 0 && index + 1 != ArcTuning::POINT_COUNT) {
            const float sample = float(index);
            const float seed = float(arc.seed % ArcTuning::SHAPE_SEED_MODULUS) * ArcTuning::SEED_PHASE_SCALE;
            const float broad = std::sin(phase * ArcTuning::BROAD_FREQUENCY + seed +
                sample * ArcTuning::BROAD_SAMPLE_SPACING);
            const float fine = std::sin(phase * ArcTuning::FINE_FREQUENCY + seed * ArcTuning::SHAPE_SEED_SCALE +
                sample * ArcTuning::FINE_SAMPLE_SPACING);
            const float alternating = (index & 1u) == 0u ? 1.0f : -1.0f;
            offset = arc.amplitude * (ArcTuning::BROAD_OFFSET_WEIGHT * broad +
                ArcTuning::FINE_OFFSET_WEIGHT * fine + ArcTuning::ALTERNATING_OFFSET_WEIGHT * alternating);
            lift = arc.amplitude * ArcTuning::LIFT_SCALE *
                std::sin(phase * ArcTuning::LIFT_FREQUENCY + seed + sample * ArcTuning::LIFT_SAMPLE_SPACING);
        }
        trail.AddPoint(XMFLOAT3(start_x + dx * t + perpendicular_x * offset,
            start_y + (end_y - start_y) * t + lift,
            start_z + dz * t + perpendicular_z * offset), width, tint);
    }
}

} // namespace

extern "C" int64_t elisa_render_scene_v1_create_electric_arc(
    float width, float amplitude, uint32_t seed) {
    if (!finite(width) || width < ArcTuning::MIN_WIDTH || width > ArcTuning::MAX_WIDTH ||
        !finite(amplitude) || amplitude < 0.0f || amplitude > ArcTuning::MAX_AMPLITUDE) {
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
    arc.depth_test = false;
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
        !bounded(end_x) || !bounded(end_y) || !bounded(end_z) || !bounded(phase, ArcTuning::MAX_PHASE) ||
        !finite(visibility) || visibility < 0.0f || visibility > 1.0f) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    const double dx = double(end_x) - start_x;
    const double dy = double(end_y) - start_y;
    const double dz = double(end_z) - start_z;
    if (dx * dx + dy * dy + dz * dz <= ArcTuning::MIN_SEGMENT_LENGTH_SQUARED) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;

    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    const size_t slot = decode_arc_handle(state, handle);
    if (slot == MAX_ELECTRIC_ARCS) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    ElectricArcSlot& arc = state.electric_arcs[slot];
    const float intensity = visibility * (ArcTuning::INTENSITY_BASE + ArcTuning::INTENSITY_VARIANCE *
        std::abs(std::sin(phase * ArcTuning::INTENSITY_FREQUENCY +
            float(arc.seed % ArcTuning::INTENSITY_SEED_MODULUS))));
    populate_arc_trail(arc.halo, arc, start_x, start_y, start_z,
        end_x, end_y, end_z, phase, intensity, true);
    populate_arc_trail(arc.core, arc, start_x, start_y, start_z,
        end_x, end_y, end_z, phase, intensity, false);
    arc.branch.Clear();
    const float branch_gate = std::abs(std::sin(phase * ArcTuning::BRANCH_FREQUENCY +
        float(arc.seed % ArcTuning::BRANCH_SEED_MODULUS)));
    if (branch_gate > ArcTuning::BRANCH_GATE_THRESHOLD && visibility > ArcTuning::MIN_BRANCH_VISIBILITY) {
        const float across = ArcTuning::BRANCH_START + ArcTuning::BRANCH_SPAN * branch_gate;
        const float branch_x = start_x + float(dx) * across;
        const float branch_y = start_y + (end_y - start_y) * across;
        const float branch_z = start_z + float(dz) * across;
        const float planar_length = std::sqrt(float(dx * dx + dz * dz));
        const float inverse_length = planar_length > ArcTuning::MIN_PLANAR_LENGTH ? 1.0f / planar_length : 1.0f;
        const float perpendicular_x = planar_length > ArcTuning::MIN_PLANAR_LENGTH ? -float(dz) * inverse_length : 1.0f;
        const float perpendicular_z = planar_length > ArcTuning::MIN_PLANAR_LENGTH ? float(dx) * inverse_length : 0.0f;
        const float direction = (arc.seed & 1u) == 0u ? 1.0f : -1.0f;
        arc.branch.subdivision = ArcTuning::BRANCH_SUBDIVISION;
        arc.branch.blendMode = wi::enums::BLENDMODE_ADDITIVE;
        arc.branch.depth_test_enabled = arc.depth_test;
        arc.branch.depth_soften = arc.depth_test ? 0.0f : ArcTuning::TRAIL_DEPTH_SOFTEN;
        arc.branch.color = XMFLOAT4(1, 1, 1, 1);
        arc.branch.width = ArcTuning::TRAIL_WIDTH;
        const XMFLOAT4 branch_color(ArcTuning::BRANCH_RED * intensity,
            ArcTuning::BRANCH_GREEN * intensity, intensity, intensity);
        arc.branch.AddPoint(XMFLOAT3(branch_x, branch_y, branch_z),
            arc.width * ArcTuning::BRANCH_WIDTH_SCALE, branch_color);
        arc.branch.AddPoint(XMFLOAT3(branch_x + perpendicular_x * arc.amplitude *
                ArcTuning::BRANCH_LENGTH_SCALE * direction,
            branch_y + arc.amplitude * ArcTuning::BRANCH_LIFT_SCALE,
            branch_z + perpendicular_z * arc.amplitude * ArcTuning::BRANCH_LENGTH_SCALE * direction),
            arc.width * ArcTuning::BRANCH_TIP_WIDTH_SCALE, branch_color);
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

// Default remains overlay for compatibility. World-space effects opt into depth.
extern "C" int32_t elisa_render_scene_v1_set_electric_arc_depth_test(int64_t handle, int32_t enabled) {
    if (enabled != 0 && enabled != 1) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    const size_t slot = decode_arc_handle(state, handle);
    if (slot == MAX_ELECTRIC_ARCS) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    ElectricArcSlot& arc = state.electric_arcs[slot];
    arc.depth_test = enabled != 0;
    arc.halo.depth_test_enabled = arc.depth_test;
    arc.core.depth_test_enabled = arc.depth_test;
    arc.branch.depth_test_enabled = arc.depth_test;
    const float soften = arc.depth_test ? 0.0f : ArcTuning::TRAIL_DEPTH_SOFTEN;
    arc.halo.depth_soften = soften;
    arc.core.depth_soften = soften;
    arc.branch.depth_soften = soften;
    return ELISA_RENDER_SCENE_OK;
}
