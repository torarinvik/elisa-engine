#pragma once

extern "C" int32_t elisa_render_scene_v1_test_environment_matches(void) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state) || state.scene == nullptr) return 0;
    const auto close = [](float left, float right) { return std::fabs(left - right) < 0.0001f; };
    const float direction_length = std::sqrt(0.2f * 0.2f + 1.0f + 0.1f * 0.1f);
    const auto& weather = state.scene->weather;
    const auto* sun = state.scene->lights.GetComponent(state.sun_entity);
    const auto* sun_transform = state.scene->transforms.GetComponent(state.sun_entity);
    if (sun_transform == nullptr) return 0;
    XMFLOAT3 transform_direction;
    XMStoreFloat3(&transform_direction, XMVector3Normalize(XMVector3TransformNormal(
        XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), XMLoadFloat4x4(&sun_transform->world))));
    return sun != nullptr && sun->type == wi::scene::LightComponent::DIRECTIONAL &&
        close(sun->color.x, 0.82f) && close(sun->color.y, 0.88f) &&
        close(sun->color.z, 1.0f) && close(sun->direction.x, -0.2f / direction_length) &&
        close(sun->direction.y, -1.0f / direction_length) &&
        close(sun->direction.z, 0.1f / direction_length) &&
        close(transform_direction.x, -0.2f / direction_length) &&
        close(transform_direction.y, -1.0f / direction_length) &&
        close(transform_direction.z, 0.1f / direction_length) &&
        close(weather.sunColor.x, 0.82f) && close(weather.sunColor.y, 0.88f) &&
        close(weather.sunColor.z, 1.0f) &&
        close(weather.sunDirection.x, -0.2f / direction_length) &&
        close(weather.sunDirection.y, -1.0f / direction_length) &&
        close(weather.sunDirection.z, 0.1f / direction_length) &&
        close(weather.ambient.x, 0.14f) && close(weather.ambient.y, 0.18f) &&
        close(weather.ambient.z, 0.25f) && close(weather.skyExposure, 1.25f) &&
        close(weather.horizon.x, 0.04f) && close(weather.horizon.y, 0.06f) &&
        close(weather.horizon.z, 0.1f) && close(weather.fogStart, 12.0f) &&
        close(weather.fogDensity, 0.02f) && weather.IsHeightFog() && weather.IsOverrideFogColor()
        ? 1 : 0;
}
