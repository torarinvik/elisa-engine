struct RenderLightSlot {
    probe::NativeLightHandle native{};
    bool live = false;
};
struct RenderCameraSlot {
    wi::ecs::Entity entity = wi::ecs::INVALID_ENTITY;
    std::unique_ptr<wi::RenderPath3D> pipeline;
    uint64_t generation = 0;
    int32_t viewport_x = 0, viewport_y = 0;
    int32_t viewport_width = 0, viewport_height = 0;
    int32_t camera_width = 0, camera_height = 0;
    float update_interval = 0.0f;
    float update_elapsed = 0.0f;
    bool render_pending = false;
    bool rendered = false;
    bool quality_profile_set = false;
    bool profile_temporal_aa = false;
    bool profile_fog = true;
    bool primary_temporal_aa_at_profile_set = false;
    bool primary_fog_at_profile_set = true;
    wi::renderer::Tonemap primary_tonemap_at_profile_set = wi::renderer::Tonemap::ACES;
    float primary_scale_at_profile_set = 1.0f;
    int32_t profile_shadow_quality = 1;
    float profile_sun_shadow_receiver_bias = 0.0f;
    int32_t primary_shadow_quality_at_profile_set = 1;
    float primary_sun_shadow_receiver_bias_at_profile_set = 0.0f;
    bool viewport_enabled = false;
    bool live = false;
};
struct PrimaryViewportState {
    int32_t x = 0;
    int32_t y = 0;
    int32_t width = 0;
    int32_t height = 0;
    bool enabled = false;
};
