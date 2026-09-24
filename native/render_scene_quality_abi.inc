extern "C" int32_t elisa_render_scene_v1_set_ambient_occlusion(int32_t enabled) {
    if (enabled != 0 && enabled != 1) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    elisa::render_scene_effects::apply_ambient_occlusion(*state.path, enabled != 0);
    return ELISA_RENDER_SCENE_OK;
}

extern "C" int32_t elisa_render_scene_v1_set_fxaa(int32_t enabled) {
    if (enabled != 0 && enabled != 1) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    elisa::render_scene_effects::apply_fxaa(*state.path, enabled != 0);
    return ELISA_RENDER_SCENE_OK;
}

extern "C" int32_t elisa_render_scene_v1_set_ambient_occlusion_settings(
    float range, float power) {
    if (!elisa::render_scene_effects::valid_ambient_occlusion_settings(range, power)) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    elisa::render_scene_effects::apply_ambient_occlusion_settings(*state.path, range, power);
    return ELISA_RENDER_SCENE_OK;
}

extern "C" int32_t elisa_render_scene_v1_set_tonemap(int32_t tonemap) {
    if (tonemap < 0 || tonemap > 2) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    const wi::renderer::Tonemap value = tonemap == 0 ? wi::renderer::Tonemap::Reinhard :
        tonemap == 1 ? wi::renderer::Tonemap::ACES : wi::renderer::Tonemap::Uchimura;
    state.path->setTonemap(value);
    return ELISA_RENDER_SCENE_OK;
}

extern "C" int32_t elisa_render_scene_v1_set_exposure(float exposure) {
    if (!std::isfinite(exposure) || exposure < 0.0f || exposure > 8.0f) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    state.path->setExposure(exposure);
    return ELISA_RENDER_SCENE_OK;
}

extern "C" int32_t elisa_render_scene_v1_set_shadow_quality(int32_t quality) {
    if (!elisa::render_scene_effects::valid_shadow_quality(quality)) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    elisa::render_scene_effects::apply_shadow_quality(quality);
    state.shadow_quality = quality;
    return ELISA_RENDER_SCENE_OK;
}

namespace {
bool valid_quality_profile(const ElisaRenderSceneQualityProfile* profile) {
    if (profile == nullptr) return false;
    return profile->tonemap >= ELISA_RENDER_SCENE_QUALITY_TONEMAP_REINHARD &&
        profile->tonemap <= ELISA_RENDER_SCENE_QUALITY_TONEMAP_UCHIMURA &&
        profile->upscaler >= ELISA_RENDER_SCENE_QUALITY_UPSCALER_NONE &&
        profile->upscaler <= ELISA_RENDER_SCENE_QUALITY_UPSCALER_FSR2 &&
        (profile->bloom == 0 || profile->bloom == 1) &&
        (profile->fxaa == 0 || profile->fxaa == 1) &&
        (profile->temporal_aa == 0 || profile->temporal_aa == 1) &&
        (profile->ambient_occlusion == 0 || profile->ambient_occlusion == 1) &&
        (profile->screen_space_reflections == 0 || profile->screen_space_reflections == 1) &&
        (profile->fog == 0 || profile->fog == 1) &&
        (profile->depth_effects == 0 || profile->depth_effects == 1) &&
        std::isfinite(profile->render_scale) && profile->render_scale >= 0.25f &&
        profile->render_scale <= 1.0f && std::isfinite(profile->bloom_threshold) &&
        profile->bloom_threshold >= 0.0f && profile->bloom_threshold <= 100.0f &&
        !(profile->temporal_aa != 0 && profile->upscaler == ELISA_RENDER_SCENE_QUALITY_UPSCALER_FSR2);
}

probe::NativePostProcessDesc quality_description(const ElisaRenderSceneQualityProfile& profile) {
    probe::NativePostProcessDesc desc;
    desc.tonemap = static_cast<probe::NativeTonemap>(profile.tonemap);
    desc.upscaler = static_cast<probe::NativeUpscaler>(profile.upscaler);
    desc.render_scale = profile.render_scale;
    desc.bloom_threshold = profile.bloom_threshold;
    desc.bloom = profile.bloom != 0;
    desc.fxaa = profile.fxaa != 0;
    desc.temporal_aa = profile.temporal_aa != 0;
    desc.ambient_occlusion = profile.ambient_occlusion != 0;
    desc.screen_space_reflections = profile.screen_space_reflections != 0;
    desc.fog = profile.fog != 0;
    desc.depth_effects = profile.depth_effects != 0;
    return desc;
}

int32_t apply_quality_profile_locked(RenderSceneService& state,
    const ElisaRenderSceneQualityProfile& profile) {
    const probe::PostProcessApply result =
        probe::PostProcessBridge(*state.path).apply(quality_description(profile), false);
    return result == probe::PostProcessApply::Invalid ?
        ELISA_RENDER_SCENE_INVALID_ARGUMENT : ELISA_RENDER_SCENE_OK;
}
}

extern "C" int32_t elisa_render_scene_v1_apply_quality_profile(
    const ElisaRenderSceneQualityProfile* profile) {
    if (!valid_quality_profile(profile)) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    return apply_quality_profile_locked(state, *profile);
}

extern "C" int32_t elisa_render_scene_v1_apply_quality_profile_with_shadow(
    const ElisaRenderSceneQualityProfile* profile, int32_t shadow_quality,
    float sun_shadow_receiver_bias) {
    if (!valid_quality_profile(profile) ||
        !elisa::render_scene_effects::valid_shadow_quality(shadow_quality) ||
        !std::isfinite(sun_shadow_receiver_bias) ||
        std::fabs(sun_shadow_receiver_bias) >
            elisa::render_scene_effects::MAX_SUN_SHADOW_RECEIVER_BIAS) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    const int32_t status = apply_quality_profile_locked(state, *profile);
    if (status != ELISA_RENDER_SCENE_OK) return status;
    elisa::render_scene_effects::apply_shadow_quality(shadow_quality);
    state.shadow_quality = shadow_quality;
    state.sun_shadow_receiver_bias = sun_shadow_receiver_bias;
    if (state.scene != nullptr) {
        wi::scene::LightComponent* sun = state.scene->lights.GetComponent(state.sun_entity);
        if (sun != nullptr && sun->type == wi::scene::LightComponent::DIRECTIONAL) {
            sun->shadow_bias = sun_shadow_receiver_bias;
        }
    }
    return ELISA_RENDER_SCENE_OK;
}

extern "C" int32_t elisa_render_scene_v1_apply_camera_quality_profile(
    int64_t camera_handle, const ElisaRenderSceneQualityProfile* profile,
    int32_t shadow_quality, float sun_shadow_receiver_bias) {
    if (!valid_quality_profile(profile) ||
        !elisa::render_scene_effects::valid_shadow_quality(shadow_quality) ||
        !std::isfinite(sun_shadow_receiver_bias) ||
        std::fabs(sun_shadow_receiver_bias) >
            elisa::render_scene_effects::MAX_SUN_SHADOW_RECEIVER_BIAS) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    size_t slot_index = MAX_RENDER_CAMERAS;
    if (!valid_camera_handle(state, camera_handle, slot_index)) {
        return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    }
    RenderCameraSlot& slot = state.cameras[slot_index];
    if (!slot.viewport_enabled || slot.pipeline == nullptr) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;

    const bool temporal_aa = wi::renderer::GetTemporalAAEnabled();
    const bool fog = state.scene->weather.IsHeightFog();
    const wi::renderer::Tonemap primary_tonemap = state.path->getTonemap();
    const float primary_scale = state.path->resolutionScale;
    const probe::PostProcessApply result = probe::PostProcessBridge(*slot.pipeline).apply(
        quality_description(*profile), false);
    wi::renderer::SetTemporalAAEnabled(temporal_aa);
    state.scene->weather.SetHeightFog(fog);
    if (result == probe::PostProcessApply::Invalid) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    slot.primary_temporal_aa_at_profile_set = temporal_aa;
    slot.primary_fog_at_profile_set = fog;
    slot.primary_tonemap_at_profile_set = primary_tonemap;
    slot.primary_scale_at_profile_set = primary_scale;
    slot.primary_shadow_quality_at_profile_set = state.shadow_quality;
    slot.primary_sun_shadow_receiver_bias_at_profile_set = state.sun_shadow_receiver_bias;
    slot.profile_temporal_aa = profile->temporal_aa != 0;
    slot.profile_fog = profile->fog != 0;
    slot.profile_shadow_quality = shadow_quality;
    slot.profile_sun_shadow_receiver_bias = sun_shadow_receiver_bias;
    slot.quality_profile_set = true;
    slot.render_pending = true;
    slot.update_elapsed = 0.0f;
    return ELISA_RENDER_SCENE_OK;
}

#if defined(ELISA_RENDER_SCENE_TEST_PROBE)
extern "C" int32_t elisa_render_scene_v1_test_quality_matches(int32_t ambient_occlusion, int32_t fxaa) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state) || state.path == nullptr) return 0;
    const wi::RenderPath3D::AO expected_ao = ambient_occlusion != 0 ?
        wi::RenderPath3D::AO_SSAO : wi::RenderPath3D::AO_DISABLED;
    return state.path->getAO() == expected_ao && state.path->getFXAAEnabled() == (fxaa != 0) ? 1 : 0;
}

extern "C" int32_t elisa_render_scene_v1_test_quality_rejects_invalid_flags(void) {
    ElisaRenderSceneQualityProfile invalid_profile{};
    invalid_profile.render_scale = 1.0f;
    invalid_profile.bloom_threshold = 1.0f;
    invalid_profile.temporal_aa = 2;
    ElisaRenderSceneQualityProfile duplicate_temporal{};
    duplicate_temporal.render_scale = 1.0f;
    duplicate_temporal.bloom_threshold = 1.0f;
    duplicate_temporal.temporal_aa = 1;
    duplicate_temporal.upscaler = ELISA_RENDER_SCENE_QUALITY_UPSCALER_FSR2;
    ElisaRenderSceneQualityProfile valid_profile{};
    valid_profile.render_scale = 1.0f;
    valid_profile.bloom_threshold = 1.0f;
    return elisa_render_scene_v1_apply_quality_profile(&invalid_profile) == ELISA_RENDER_SCENE_INVALID_ARGUMENT &&
        elisa_render_scene_v1_apply_quality_profile(&duplicate_temporal) == ELISA_RENDER_SCENE_INVALID_ARGUMENT &&
        elisa_render_scene_v1_apply_quality_profile_with_shadow(&valid_profile, 3, 0.0f) == ELISA_RENDER_SCENE_INVALID_ARGUMENT &&
        elisa_render_scene_v1_apply_quality_profile_with_shadow(&valid_profile, 2, 0.02f) == ELISA_RENDER_SCENE_INVALID_ARGUMENT &&
        elisa_render_scene_v1_set_ambient_occlusion(2) == ELISA_RENDER_SCENE_INVALID_ARGUMENT &&
        elisa_render_scene_v1_set_ambient_occlusion_settings(0.0f, 1.0f) == ELISA_RENDER_SCENE_INVALID_ARGUMENT &&
        elisa_render_scene_v1_set_ambient_occlusion_settings(1.0f, 9.0f) == ELISA_RENDER_SCENE_INVALID_ARGUMENT &&
        elisa_render_scene_v1_set_fxaa(-1) == ELISA_RENDER_SCENE_INVALID_ARGUMENT &&
        elisa_render_scene_v1_set_tonemap(3) == ELISA_RENDER_SCENE_INVALID_ARGUMENT &&
        elisa_render_scene_v1_set_exposure(-1.0f) == ELISA_RENDER_SCENE_INVALID_ARGUMENT &&
        elisa_render_scene_v1_set_exposure(9.0f) == ELISA_RENDER_SCENE_INVALID_ARGUMENT ? 1 : 0;
}

extern "C" int32_t elisa_render_scene_v1_test_ambient_occlusion_matches(
    float range, float power) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state) || state.path == nullptr) return 0;
    return state.path->getAORange() == range && state.path->getAOPower() == power ? 1 : 0;
}

extern "C" int32_t elisa_render_scene_v1_test_postprocess_matches(
    int32_t tonemap, float exposure) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state) || state.path == nullptr) return 0;
    const wi::renderer::Tonemap expected = tonemap == 0 ? wi::renderer::Tonemap::Reinhard :
        tonemap == 1 ? wi::renderer::Tonemap::ACES : wi::renderer::Tonemap::Uchimura;
    return state.path->getTonemap() == expected && state.path->getExposure() == exposure ? 1 : 0;
}

extern "C" int32_t elisa_render_scene_v1_test_shadow_quality_matches(int32_t quality) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state) || state.path == nullptr) return 0;
    return state.shadow_quality == quality ? 1 : 0;
}

extern "C" int32_t elisa_render_scene_v1_test_quality_shadow_matches(
    int32_t quality, float receiver_bias) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state) ||
        state.shadow_quality != quality ||
        std::fabs(state.sun_shadow_receiver_bias - receiver_bias) >= 0.000001f) return 0;
    if (state.scene != nullptr && state.sun_entity != wi::ecs::INVALID_ENTITY) {
        const auto* sun = state.scene->lights.GetComponent(state.sun_entity);
        if (sun != nullptr && sun->type == wi::scene::LightComponent::DIRECTIONAL &&
            std::fabs(sun->shadow_bias - receiver_bias) >= 0.000001f) return 0;
    }
    return 1;
}

extern "C" int32_t elisa_render_scene_v1_test_quality_profile_matches(
    int32_t tonemap, float render_scale, float bloom_threshold,
    int32_t bloom, int32_t fxaa, int32_t temporal_aa, int32_t ambient_occlusion,
    int32_t screen_space_reflections, int32_t fog, int32_t depth_effects) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state) || state.path == nullptr ||
        state.scene == nullptr) return 0;
    const wi::renderer::Tonemap expected = tonemap == 0 ? wi::renderer::Tonemap::Reinhard :
        tonemap == 1 ? wi::renderer::Tonemap::ACES : wi::renderer::Tonemap::Uchimura;
    const bool fog_matches = state.scene->weather.IsHeightFog() == (fog != 0);
    return state.path->getTonemap() == expected &&
        std::abs(state.path->resolutionScale - render_scale) < 1.0e-5f &&
        std::abs(state.path->getBloomThreshold() - bloom_threshold) < 1.0e-5f &&
        state.path->getBloomEnabled() == (bloom != 0) &&
        state.path->getFXAAEnabled() == (fxaa != 0) &&
        wi::renderer::GetTemporalAAEnabled() == (temporal_aa != 0) &&
        state.path->getAO() == (ambient_occlusion != 0 ? wi::RenderPath3D::AO_SSAO : wi::RenderPath3D::AO_DISABLED) &&
        state.path->getSSREnabled() == (screen_space_reflections != 0) &&
        state.path->getDepthOfFieldEnabled() == (depth_effects != 0) &&
        fog_matches ? 1 : 0;
}

extern "C" int32_t elisa_render_scene_v1_test_temporal_history_resources(
    float expected_scale, int32_t expected_valid) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state) || state.path == nullptr ||
        !std::isfinite(expected_scale) || expected_scale < 0.25f || expected_scale > 1.0f ||
        (expected_valid != 0 && expected_valid != 1)) return 0;
    if (std::fabs(state.path->resolutionScale - expected_scale) >= 0.00001f) return 0;
    const wi::renderer::TemporalAAResources& history = state.path->temporalAAResources;
    if (expected_valid == 0) return history.IsValid() ? 0 : 1;
    if (!history.IsValid()) return 0;
    const XMUINT2 expected_resolution = state.path->GetInternalResolution();
    for (const wi::graphics::Texture& texture : history.texture_temporal) {
        if (!texture.IsValid() || texture.desc.width != expected_resolution.x ||
            texture.desc.height != expected_resolution.y) return 0;
    }
    return 1;
}

extern "C" int32_t elisa_render_scene_v1_test_camera_quality_profile_matches(
    int32_t tonemap, int32_t bloom, float threshold,
    int32_t fxaa, int32_t ambient_occlusion, int32_t reflections,
    int32_t depth_effects, float render_scale, int32_t shadow_quality,
    float sun_shadow_receiver_bias) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state) || state.path == nullptr ||
        state.scene == nullptr) return 0;
    const RenderCameraSlot* selected = nullptr;
    for (const RenderCameraSlot& candidate : state.cameras) {
        if (candidate.live && candidate.viewport_enabled && candidate.quality_profile_set &&
            candidate.pipeline != nullptr) {
            selected = &candidate;
            break;
        }
    }
    if (selected == nullptr) return 0;
    const RenderCameraSlot& slot = *selected;
    const wi::renderer::Tonemap expected = tonemap == 0 ? wi::renderer::Tonemap::Reinhard :
        tonemap == 1 ? wi::renderer::Tonemap::ACES : wi::renderer::Tonemap::Uchimura;
    return slot.pipeline->getTonemap() == expected &&
        slot.pipeline->getBloomEnabled() == (bloom != 0) &&
        std::fabs(slot.pipeline->getBloomThreshold() - threshold) < 0.000001f &&
        slot.pipeline->getFXAAEnabled() == (fxaa != 0) &&
        slot.pipeline->getAOEnabled() == (ambient_occlusion != 0) &&
        slot.pipeline->getSSREnabled() == (reflections != 0) &&
        slot.pipeline->getDepthOfFieldEnabled() == (depth_effects != 0) &&
        std::fabs(slot.pipeline->resolutionScale - render_scale) < 0.000001f &&
        slot.profile_shadow_quality == shadow_quality &&
        std::fabs(slot.profile_sun_shadow_receiver_bias - sun_shadow_receiver_bias) < 0.000001f &&
        !slot.profile_temporal_aa && !slot.profile_fog &&
        wi::renderer::GetTemporalAAEnabled() == slot.primary_temporal_aa_at_profile_set &&
        state.scene->weather.IsHeightFog() == slot.primary_fog_at_profile_set &&
        state.path->getTonemap() == slot.primary_tonemap_at_profile_set &&
        std::fabs(state.path->resolutionScale - slot.primary_scale_at_profile_set) < 0.000001f &&
        state.shadow_quality == slot.primary_shadow_quality_at_profile_set &&
        std::fabs(state.sun_shadow_receiver_bias -
            slot.primary_sun_shadow_receiver_bias_at_profile_set) < 0.000001f ? 1 : 0;
}
#endif
