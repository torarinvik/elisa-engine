#pragma once

extern "C" int32_t elisa_render_scene_v1_test_environment_matches(void) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state) || state.scene == nullptr) return 0;
    const auto close = [](float left, float right) { return std::fabs(left - right) < 0.0001f; };
    // The test authors the sun at (-0.2, -1, 0.1) in Elisa space; Wicked
    // stores its reflection.
    const float direction_length = std::sqrt(0.2f * 0.2f + 1.0f + 0.1f * 0.1f);
    const XMFLOAT3 expected = probe::coordinates::to_wicked_direction(XMFLOAT3(
        -0.2f / direction_length, -1.0f / direction_length, 0.1f / direction_length));
    const auto& weather = state.scene->weather;
    const auto* sun = state.scene->lights.GetComponent(state.sun_entity);
    const auto* sun_transform = state.scene->transforms.GetComponent(state.sun_entity);
    if (sun_transform == nullptr) return 0;
    XMFLOAT3 transform_direction;
    XMStoreFloat3(&transform_direction, XMVector3Normalize(XMVector3TransformNormal(
        XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), XMLoadFloat4x4(&sun_transform->world))));
    return sun != nullptr && sun->type == wi::scene::LightComponent::DIRECTIONAL &&
        close(sun->color.x, 0.82f) && close(sun->color.y, 0.88f) &&
        close(sun->color.z, 1.0f) && close(sun->direction.x, expected.x) &&
        close(sun->direction.y, expected.y) &&
        close(sun->direction.z, expected.z) &&
        close(transform_direction.x, expected.x) &&
        close(transform_direction.y, expected.y) &&
        close(transform_direction.z, expected.z) &&
        close(weather.sunColor.x, 0.82f) && close(weather.sunColor.y, 0.88f) &&
        close(weather.sunColor.z, 1.0f) &&
        close(weather.sunDirection.x, expected.x) &&
        close(weather.sunDirection.y, expected.y) &&
        close(weather.sunDirection.z, expected.z) &&
        close(weather.ambient.x, 0.14f) && close(weather.ambient.y, 0.18f) &&
        close(weather.ambient.z, 0.25f) && close(weather.skyExposure, 1.25f) &&
        close(weather.horizon.x, 0.04f) && close(weather.horizon.y, 0.06f) &&
        close(weather.horizon.z, 0.1f) && close(weather.fogStart, 12.0f) &&
        close(weather.fogDensity, 0.02f) && weather.IsHeightFog() && weather.IsOverrideFogColor()
        ? 1 : 0;
}

extern "C" int32_t elisa_render_scene_v1_test_art_material_settings(void) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state)) return 0;
    for (const auto& instance : state.instances) {
        if (!instance.live) continue;
        const auto* material = state.scene->materials.GetComponent(instance.entity);
        const auto* object = state.scene->objects.GetComponent(instance.entity);
        if (material && object && material->shaderType == wi::scene::MaterialComponent::SHADERTYPE_PBR &&
            std::fabs(material->roughness - 0.65f) < 0.0001f &&
            std::fabs(material->metalness - 0.3f) < 0.0001f &&
            material->texMulAdd.x == 1 && material->texMulAdd.y == -1 &&
            material->texMulAdd.z == 0 && material->texMulAdd.w == 1 &&
            material->IsCastingShadow() && object->IsCastingShadow()) return 1;
    }
    return 0;
}

extern "C" int32_t elisa_render_scene_v1_test_alpha_mode(
    int32_t mode, float cutoff, int32_t double_sided) {
    if (mode < 0 || mode > 2 || (double_sided != 0 && double_sided != 1)) return 0;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state)) return 0;
    const bool blended = mode == 2;
    const float expected_alpha_ref = mode == 1 ? cutoff : 1.0f;
    for (const auto& instance : state.instances) {
        if (!instance.live) continue;
        const auto* material = state.scene->materials.GetComponent(instance.entity);
        const auto* object = state.scene->objects.GetComponent(instance.entity);
        if (material && object && material->shaderType == wi::scene::MaterialComponent::SHADERTYPE_PBR &&
            std::fabs(material->roughness - 0.65f) < 0.0001f &&
            std::fabs(material->metalness - 0.3f) < 0.0001f) {
            return material->userBlendMode == (blended ? wi::enums::BLENDMODE_ALPHA : wi::enums::BLENDMODE_OPAQUE) &&
                std::fabs(material->alphaRef - expected_alpha_ref) < 0.0001f &&
                material->IsDoubleSided() == (double_sided != 0) &&
                material->IsCastingShadow() == !blended &&
                object->IsCastingShadow() == !blended ? 1 : 0;
        }
    }
    return 0;
}

extern "C" int32_t elisa_render_scene_v1_test_sun_shadows_match(int32_t enabled) {
    if (enabled != 0 && enabled != 1) return 0;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state) || state.path == nullptr) return 0;
    const auto* sun = state.scene->lights.GetComponent(state.sun_entity);
    const bool expected = enabled != 0;
    return sun != nullptr && sun->IsCastingShadow() == expected &&
        state.path->getShadowsEnabled() == expected ? 1 : 0;
}

extern "C" int32_t elisa_render_scene_v1_test_sun_cascade_distances_match(
    float near_end, float middle_end, float far_end) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state) || state.scene == nullptr) return 0;
    const auto* sun = state.scene->lights.GetComponent(state.sun_entity);
    if (sun == nullptr || sun->cascade_distances.size() != 3) return 0;
    const auto close = [](float left, float right) { return std::fabs(left - right) < 0.0001f; };
    return close(sun->cascade_distances[0], near_end) &&
        close(sun->cascade_distances[1], middle_end) &&
        close(sun->cascade_distances[2], far_end) ? 1 : 0;
}
