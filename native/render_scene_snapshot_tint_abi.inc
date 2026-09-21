// Per-instance color and emissive multipliers for snapshot rows. Wicked applies
// ObjectComponent::color to the shared material's base color and
// ObjectComponent::emissiveColor (rgb * w) to its emission, so a tint never
// mutates the material other instances share. Validated here, applied at commit.
extern "C" int32_t elisa_render_scene_v1_snapshot_stage_tint(int64_t render_id,
    float red, float green, float blue,
    float emissive_red, float emissive_green, float emissive_blue, float emissive_strength) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    if (!state.snapshot_transaction_active || render_id <= 0 ||
        !snapshot_tint_color_valid(red, green, blue) ||
        !elisa::render_scene_effects::valid_emission(emissive_red, emissive_green, emissive_blue,
            emissive_strength)) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    for (size_t index = 0; index < state.snapshot_row_count; ++index) {
        SnapshotStageRow& row = state.snapshot_rows[index];
        if (row.render_id != render_id) continue;
        if (row.tinted) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
        row.tint_color[0] = red;
        row.tint_color[1] = green;
        row.tint_color[2] = blue;
        row.tint_emissive[0] = emissive_red;
        row.tint_emissive[1] = emissive_green;
        row.tint_emissive[2] = emissive_blue;
        row.tint_emissive[3] = emissive_strength;
        row.tinted = true;
#if defined(ELISA_RENDER_SCENE_TEST_PROBE)
        ++state.snapshot_test_transaction_api_calls;
#endif
        return ELISA_RENDER_SCENE_OK;
    }
    return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
}
