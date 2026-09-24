extern "C" int32_t elisa_render_scene_v1_register_snapshot_material_asset_with_pbr_extensions(
    uint64_t high, uint64_t low,
    uint64_t base_color_high, uint64_t base_color_low,
    uint64_t normal_high, uint64_t normal_low,
    uint64_t surface_high, uint64_t surface_low,
    uint64_t emissive_high, uint64_t emissive_low,
    uint64_t clearcoat_high, uint64_t clearcoat_low,
    uint64_t clearcoat_roughness_high, uint64_t clearcoat_roughness_low,
    uint64_t clearcoat_normal_high, uint64_t clearcoat_normal_low,
    float red, float green, float blue, float alpha,
    float metallic, float roughness, float normal_scale, float occlusion_strength,
    float clearcoat_factor, float clearcoat_roughness_factor, float clearcoat_normal_scale,
    float emissive_red, float emissive_green, float emissive_blue,
    float alpha_cutoff, int32_t alpha_mode, int32_t double_sided, int32_t occlusion) {
    SnapshotMaterialAssetSlot values;
    values.base_color[0] = red; values.base_color[1] = green;
    values.base_color[2] = blue; values.base_color[3] = alpha;
    values.metallic = metallic;
    values.roughness = roughness;
    values.normal_scale = normal_scale;
    values.occlusion_strength = occlusion_strength;
    values.clearcoat_factor = clearcoat_factor;
    values.clearcoat_roughness = clearcoat_roughness_factor;
    values.clearcoat_normal_scale = clearcoat_normal_scale;
    values.emissive[0] = emissive_red; values.emissive[1] = emissive_green; values.emissive[2] = emissive_blue;
    values.alpha_cutoff = alpha_cutoff;
    values.alpha_mode = alpha_mode;
    values.double_sided = double_sided != 0;
    values.occlusion = occlusion != 0;
    values.texture_high = {base_color_high, normal_high, surface_high, emissive_high, 0,
        clearcoat_high, clearcoat_roughness_high, clearcoat_normal_high};
    values.texture_low = {base_color_low, normal_low, surface_low, emissive_low, 0,
        clearcoat_low, clearcoat_roughness_low, clearcoat_normal_low};
    if ((high == 0 && low == 0) || !snapshot_material_values_valid(values) ||
        (double_sided != 0 && double_sided != 1) || (occlusion != 0 && occlusion != 1)) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    if (state.snapshot_transaction_active) return ELISA_RENDER_SCENE_BATCH_ACTIVE;
    bool created = false;
    return register_snapshot_material_locked(state, high, low, values, created);
}

extern "C" int32_t elisa_render_scene_v1_register_snapshot_material_asset_with_material_factors(
    uint64_t high, uint64_t low,
    uint64_t base_color_high, uint64_t base_color_low,
    uint64_t normal_high, uint64_t normal_low,
    uint64_t surface_high, uint64_t surface_low,
    uint64_t emissive_high, uint64_t emissive_low,
    float red, float green, float blue, float alpha,
    float metallic, float roughness, float normal_scale, float occlusion_strength,
    float emissive_red, float emissive_green, float emissive_blue,
    float alpha_cutoff, int32_t alpha_mode, int32_t double_sided, int32_t occlusion) {
    return elisa_render_scene_v1_register_snapshot_material_asset_with_pbr_extensions(
        high, low, base_color_high, base_color_low, normal_high, normal_low,
        surface_high, surface_low, emissive_high, emissive_low,
        0, 0, 0, 0, 0, 0,
        red, green, blue, alpha, metallic, roughness, normal_scale, occlusion_strength,
        0.0f, 0.0f, 1.0f, emissive_red, emissive_green, emissive_blue,
        alpha_cutoff, alpha_mode, double_sided, occlusion);
}
