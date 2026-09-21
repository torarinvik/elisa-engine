// Private Elisa-owned environment updates for the active Wicked scene.
extern "C" int32_t elisa_render_scene_v1_set_environment(
    float sun_direction_x, float sun_direction_y, float sun_direction_z,
    float sun_red, float sun_green, float sun_blue,
    float ambient_red, float ambient_green, float ambient_blue,
    float sky_exposure,
    float fog_red, float fog_green, float fog_blue,
    float fog_start, float fog_density, int32_t fog_enabled) {
    const float values[] = {
        sun_direction_x, sun_direction_y, sun_direction_z,
        sun_red, sun_green, sun_blue,
        ambient_red, ambient_green, ambient_blue,
        sky_exposure, fog_red, fog_green, fog_blue, fog_start, fog_density,
    };
    for (float value : values) {
        if (!bounded(value)) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    if (sun_red < 0.0f || sun_green < 0.0f || sun_blue < 0.0f ||
        ambient_red < 0.0f || ambient_green < 0.0f || ambient_blue < 0.0f ||
        sky_exposure < 0.0f || fog_red < 0.0f || fog_green < 0.0f || fog_blue < 0.0f ||
        fog_start < 0.0f || fog_density < 0.0f || (fog_enabled != 0 && fog_enabled != 1)) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    const XMFLOAT3 wicked_sun_direction = probe::coordinates::to_wicked_direction(
        XMFLOAT3(sun_direction_x, sun_direction_y, sun_direction_z));
    const XMVECTOR sun_direction = XMVectorSet(wicked_sun_direction.x, wicked_sun_direction.y,
        wicked_sun_direction.z, 0.0f);
    if (XMVectorGetX(XMVector3LengthSq(sun_direction)) <= 1.0e-8f) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }

    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    if (state.scene == nullptr) return ELISA_RENDER_SCENE_BACKEND_FAILED;

    if (state.sun_entity == wi::ecs::INVALID_ENTITY) {
        try {
            state.sun_entity = state.scene->Entity_CreateLight("elisa_render_scene_sun",
                XMFLOAT3(0.0f, 0.0f, 0.0f), XMFLOAT3(sun_red, sun_green, sun_blue),
                1.0f, 1.0f, wi::scene::LightComponent::DIRECTIONAL);
        } catch (...) {
            return ELISA_RENDER_SCENE_BACKEND_FAILED;
        }
        if (state.sun_entity == wi::ecs::INVALID_ENTITY) return ELISA_RENDER_SCENE_BACKEND_FAILED;
    }
    wi::scene::LightComponent* sun = state.scene->lights.GetComponent(state.sun_entity);
    wi::scene::TransformComponent* sun_transform = state.scene->transforms.GetComponent(state.sun_entity);
    if (sun == nullptr || sun_transform == nullptr) return ELISA_RENDER_SCENE_BACKEND_FAILED;
    sun->SetType(wi::scene::LightComponent::DIRECTIONAL);
    sun->color = XMFLOAT3(sun_red, sun_green, sun_blue);
    sun->intensity = 1.0f;
    const XMVECTOR normalized_direction = XMVector3Normalize(sun_direction);
    const XMVECTOR local_up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    const XMVECTOR axis = XMVector3Cross(local_up, normalized_direction);
    const float dot = std::clamp(XMVectorGetX(XMVector3Dot(local_up, normalized_direction)), -1.0f, 1.0f);
    XMVECTOR rotation = XMQuaternionIdentity();
    if (dot < -0.999999f) {
        rotation = XMQuaternionRotationAxis(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), XM_PI);
    } else if (dot < 0.999999f) {
        rotation = XMQuaternionRotationAxis(XMVector3Normalize(axis), std::acos(dot));
    }
    XMStoreFloat4(&sun_transform->rotation_local, rotation);
    sun_transform->SetDirty();
    sun_transform->UpdateTransform();
    XMStoreFloat3(&sun->direction, normalized_direction);
    sun->SetCastShadow(false);

    auto& weather = state.scene->weather;
    weather.sunColor = XMFLOAT3(sun_red, sun_green, sun_blue);
    XMStoreFloat3(&weather.sunDirection, normalized_direction);
    weather.ambient = XMFLOAT3(ambient_red, ambient_green, ambient_blue);
    weather.skyExposure = sky_exposure;
    weather.horizon = XMFLOAT3(fog_red, fog_green, fog_blue);
    weather.fogStart = fog_start;
    weather.fogDensity = fog_enabled ? fog_density : 0.0f;
    weather.SetHeightFog(fog_enabled != 0);
    weather.SetOverrideFogColor(fog_enabled != 0);
    return ELISA_RENDER_SCENE_OK;
}

extern "C" int32_t elisa_render_scene_v1_set_sun_shadows(int32_t enabled) {
    if (enabled != 0 && enabled != 1) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    auto* sun = state.scene->lights.GetComponent(state.sun_entity);
    if (!sun) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    sun->SetCastShadow(enabled != 0);
    state.path->setShadowsEnabled(enabled != 0);
    return ELISA_RENDER_SCENE_OK;
}
