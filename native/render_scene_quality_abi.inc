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
    return elisa_render_scene_v1_set_ambient_occlusion(2) == ELISA_RENDER_SCENE_INVALID_ARGUMENT &&
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
#endif
