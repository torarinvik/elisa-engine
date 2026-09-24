#pragma once

// Generation-checked lighting and environment ownership for the Wicked
// backend. Portable descriptors cross this boundary; Wicked entities do not.
#include "probe_core.h"
#include "wiScene.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>

namespace probe {

enum class NativeLightKind : uint8_t { Directional, Point, Spot, Rectangle };

struct NativeLightDesc {
    NativeLightKind kind = NativeLightKind::Point;
    XMFLOAT3 color = XMFLOAT3(1, 1, 1);
    XMFLOAT3 position = XMFLOAT3(0, 0, 0);
    XMFLOAT3 direction = XMFLOAT3(0, -1, 0);
    float intensity = 1.0f;
    float range = 10.0f;
    float outer_cone = XM_PIDIV4;
    float inner_cone = 0.0f;
    bool cast_shadow = false;
    int32_t shadow_resolution = 0;
    float rectangle_width = 0.0f;
    float rectangle_height = 0.0f;
    bool volumetrics_enabled = false;
    float volumetric_boost = 0.0f;
};

struct NativeEnvironmentDesc {
    XMFLOAT3 sun_color = XMFLOAT3(0.0f, 0.0f, 0.0f);
    XMFLOAT3 sun_direction = XMFLOAT3(0.0f, -1.0f, 0.0f);
    XMFLOAT3 ambient = XMFLOAT3(0.2f, 0.2f, 0.2f);
    XMFLOAT3 fog_color = XMFLOAT3(0.0f, 0.0f, 0.0f);
    float sky_exposure = 1.0f;
    float fog_start = 100.0f;
    float fog_density = 0.0f;
    bool fog_enabled = false;
};

struct NativeLightHandle {
    uint32_t slot = UINT32_MAX;
    uint32_t generation = 0;
    uintptr_t owner = 0;
};

struct NativeEnvironmentHandle {
    uint32_t slot = UINT32_MAX;
    uint32_t generation = 0;
    uintptr_t owner = 0;
};

class LightingBridge {
public:
    static constexpr uint32_t MAX_LIGHTS = 32;
    static constexpr uint32_t MAX_ENVIRONMENTS = 8;
    static constexpr int32_t MIN_SHADOW_RESOLUTION = 16;
    static constexpr int32_t MAX_SHADOW_RESOLUTION = 2048;
    static constexpr float MAX_RECTANGLE_DIMENSION = 1000000.0f;
    static constexpr float MAX_VOLUMETRIC_BOOST = 8.0f;

    explicit LightingBridge(wi::scene::Scene& scene)
        : scene_(scene), owner_(reinterpret_cast<uintptr_t>(this)) {}
    LightingBridge(const LightingBridge&) = delete;
    LightingBridge& operator=(const LightingBridge&) = delete;

    NativeLightHandle create_light(const NativeLightDesc& desc) {
        if (!valid_desc(desc)) return {};
        const uint32_t slot = free_light();
        if (slot == MAX_LIGHTS) return {};
        const auto entity = scene_.Entity_CreateLight(
            "elisa_light_" + std::to_string(slot), desc.position, desc.color,
            desc.intensity, desc.range, light_type(desc.kind), desc.outer_cone, desc.inner_cone);
        if (entity == wi::ecs::INVALID_ENTITY) return {};
        auto& state = lights_[slot];
        state.generation = state.generation == UINT32_MAX ? 0 : state.generation + 1;
        if (state.generation == 0) {
            scene_.Entity_Remove(entity);
            return {};
        }
        state.entity = entity;
        state.live = true;
        apply(entity, desc);
        return {slot, state.generation, owner_};
    }

    bool update_light(NativeLightHandle handle, const NativeLightDesc& desc) {
        if (!live(handle) || !valid_desc(desc)) return false;
        apply(lights_[handle.slot].entity, desc);
        return true;
    }

    bool light_matches(NativeLightHandle handle, const NativeLightDesc& desc) const {
        if (!live(handle)) return false;
        const wi::ecs::Entity entity = lights_[handle.slot].entity;
        const auto* light = scene_.lights.GetComponent(entity);
        const auto* transform = scene_.transforms.GetComponent(entity);
        if (light == nullptr || transform == nullptr) return false;
        const auto close = [](float left, float right) { return std::fabs(left - right) < 0.0001f; };
        const XMFLOAT3 position = transform->translation_local;
        const float direction_length = XMVectorGetX(XMVector3Length(XMLoadFloat3(&desc.direction)));
        const XMFLOAT3 direction = desc.kind == NativeLightKind::Point ? XMFLOAT3(0.0f, -1.0f, 0.0f) : XMFLOAT3(
            desc.direction.x / direction_length, desc.direction.y / direction_length,
            desc.direction.z / direction_length);
        const XMVECTOR local_axis = light_forward_axis(desc.kind);
        XMFLOAT3 rendered_direction;
        XMStoreFloat3(&rendered_direction, XMVector3Normalize(XMVector3TransformNormal(
            local_axis, transform->GetWorldMatrix())));
        return light->type == light_type(desc.kind) && close(position.x, desc.position.x) &&
            close(position.y, desc.position.y) && close(position.z, desc.position.z) &&
            close(light->direction.x, direction.x) && close(light->direction.y, direction.y) &&
            close(light->direction.z, direction.z) && close(light->color.x, desc.color.x) &&
            close(light->color.y, desc.color.y) && close(light->color.z, desc.color.z) &&
            (desc.kind == NativeLightKind::Point || (close(rendered_direction.x, direction.x) &&
                close(rendered_direction.y, direction.y) && close(rendered_direction.z, direction.z))) &&
            close(light->intensity, desc.intensity) && close(light->range, desc.range) &&
            close(light->innerConeAngle, desc.kind == NativeLightKind::Spot ? desc.inner_cone : 0.0f) &&
            close(light->outerConeAngle, desc.kind == NativeLightKind::Spot ? desc.outer_cone : 0.0f) &&
            close(light->length, desc.kind == NativeLightKind::Rectangle ? desc.rectangle_width : 0.0f) &&
            close(light->height, desc.kind == NativeLightKind::Rectangle ? desc.rectangle_height : 0.0f) &&
            light->IsCastingShadow() == desc.cast_shadow &&
            light->IsVolumetricsEnabled() == desc.volumetrics_enabled &&
            close(light->volumetric_boost, desc.volumetric_boost) &&
            light->forced_shadow_resolution == (desc.shadow_resolution == 0 ? -1 : desc.shadow_resolution);
    }

    bool destroy_light(NativeLightHandle handle) {
        if (!live(handle)) return false;
        scene_.Entity_Remove(lights_[handle.slot].entity);
        lights_[handle.slot].live = false;
        lights_[handle.slot].entity = wi::ecs::INVALID_ENTITY;
        return true;
    }

    bool live(NativeLightHandle handle) const {
        return handle.owner == owner_ && handle.slot < MAX_LIGHTS && lights_[handle.slot].live &&
            lights_[handle.slot].generation == handle.generation;
    }

    wi::ecs::Entity entity(NativeLightHandle handle) const {
        return live(handle) ? lights_[handle.slot].entity : wi::ecs::INVALID_ENTITY;
    }

    NativeEnvironmentHandle create_environment(uint32_t resolution, float view_distance, bool realtime) {
        if (!valid_resolution(resolution) || !std::isfinite(view_distance) || view_distance == 0.0f) return {};
        const uint32_t slot = free_environment();
        if (slot == MAX_ENVIRONMENTS) return {};
        const auto entity = scene_.Entity_CreateEnvironmentProbe("elisa_environment_" + std::to_string(slot));
        if (entity == wi::ecs::INVALID_ENTITY) return {};
        auto* probe = scene_.probes.GetComponent(entity);
        auto& state = environments_[slot];
        state.generation = state.generation == UINT32_MAX ? 0 : state.generation + 1;
        if (probe == nullptr || state.generation == 0) {
            scene_.Entity_Remove(entity);
            return {};
        }
        probe->resolution = resolution;
        probe->view_distance = view_distance;
        probe->SetRealTime(realtime);
        state.entity = entity;
        state.live = true;
        return {slot, state.generation, owner_};
    }

    bool destroy_environment(NativeEnvironmentHandle handle) {
        if (!live(handle)) return false;
        scene_.Entity_Remove(environments_[handle.slot].entity);
        environments_[handle.slot].live = false;
        environments_[handle.slot].entity = wi::ecs::INVALID_ENTITY;
        return true;
    }

    bool live(NativeEnvironmentHandle handle) const {
        return handle.owner == owner_ && handle.slot < MAX_ENVIRONMENTS &&
            environments_[handle.slot].live && environments_[handle.slot].generation == handle.generation;
    }

    bool apply_environment(const NativeEnvironmentDesc& desc) {
        if (!valid_environment(desc)) return false;
        auto& weather = scene_.weather;
        weather.sunColor = desc.sun_color;
        XMStoreFloat3(&weather.sunDirection, XMVector3Normalize(XMLoadFloat3(&desc.sun_direction)));
        weather.ambient = desc.ambient;
        weather.skyExposure = desc.sky_exposure;
        weather.horizon = desc.fog_color;
        weather.fogStart = desc.fog_start;
        weather.fogDensity = desc.fog_enabled ? desc.fog_density : 0.0f;
        weather.SetHeightFog(desc.fog_enabled);
        weather.SetOverrideFogColor(desc.fog_enabled);
        return true;
    }

private:
    struct LightState { wi::ecs::Entity entity = wi::ecs::INVALID_ENTITY; uint32_t generation = 0; bool live = false; };
    struct EnvironmentState { wi::ecs::Entity entity = wi::ecs::INVALID_ENTITY; uint32_t generation = 0; bool live = false; };

    static bool finite_color(const XMFLOAT3& color) {
        return std::isfinite(color.x) && std::isfinite(color.y) && std::isfinite(color.z) &&
            color.x >= 0.0f && color.y >= 0.0f && color.z >= 0.0f;
    }

    static bool valid_environment(const NativeEnvironmentDesc& desc) {
        const auto nonnegative = [](const XMFLOAT3& value) {
            return finite_color(value) && value.x >= 0.0f && value.y >= 0.0f && value.z >= 0.0f;
        };
        return nonnegative(desc.sun_color) && nonnegative(desc.ambient) && finite_color(desc.fog_color) &&
            std::isfinite(desc.sun_direction.x) && std::isfinite(desc.sun_direction.y) &&
            std::isfinite(desc.sun_direction.z) && XMVectorGetX(XMVector3Length(XMLoadFloat3(&desc.sun_direction))) > 0.0001f &&
            std::isfinite(desc.sky_exposure) && desc.sky_exposure >= 0.0f &&
            std::isfinite(desc.fog_start) && desc.fog_start >= 0.0f &&
            std::isfinite(desc.fog_density) && desc.fog_density >= 0.0f;
    }

    static bool valid_desc(const NativeLightDesc& desc) {
        const auto finite_vector = [](const XMFLOAT3& value) {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        };
        const float direction_length = XMVectorGetX(XMVector3Length(XMLoadFloat3(&desc.direction)));
        const bool valid_kind = desc.kind == NativeLightKind::Directional || desc.kind == NativeLightKind::Point ||
            desc.kind == NativeLightKind::Spot || desc.kind == NativeLightKind::Rectangle;
        const bool valid_spot_cone = desc.kind != NativeLightKind::Spot ||
            (desc.outer_cone > 0.0f && desc.inner_cone >= 0.0f && desc.inner_cone <= desc.outer_cone);
        const bool valid_rectangle_size = std::isfinite(desc.rectangle_width) && std::isfinite(desc.rectangle_height) &&
            (desc.kind == NativeLightKind::Rectangle ? desc.rectangle_width > 0.0f && desc.rectangle_height > 0.0f &&
                desc.rectangle_width <= MAX_RECTANGLE_DIMENSION && desc.rectangle_height <= MAX_RECTANGLE_DIMENSION :
                desc.rectangle_width == 0.0f && desc.rectangle_height == 0.0f);
        return valid_kind && finite_color(desc.color) && finite_vector(desc.position) && finite_vector(desc.direction) &&
            (desc.kind == NativeLightKind::Point || direction_length > 0.0001f) &&
            std::isfinite(desc.intensity) && desc.intensity >= 0.0f &&
            std::isfinite(desc.range) && desc.range > 0.0f && std::isfinite(desc.outer_cone) &&
            std::isfinite(desc.inner_cone) && valid_spot_cone && valid_shadow_resolution(desc.shadow_resolution) &&
            valid_rectangle_size && std::isfinite(desc.volumetric_boost) && desc.volumetric_boost >= 0.0f &&
            desc.volumetric_boost <= MAX_VOLUMETRIC_BOOST;
    }

    static bool valid_shadow_resolution(int32_t resolution) {
        return resolution == 0 || (resolution >= MIN_SHADOW_RESOLUTION && resolution <= MAX_SHADOW_RESOLUTION &&
            (resolution & (resolution - 1)) == 0);
    }

    static bool valid_resolution(uint32_t resolution) {
        return resolution >= 16 && resolution <= 2048 && (resolution & (resolution - 1)) == 0;
    }

    static wi::scene::LightComponent::LightType light_type(NativeLightKind kind) {
        return kind == NativeLightKind::Directional ? wi::scene::LightComponent::DIRECTIONAL :
            kind == NativeLightKind::Spot ? wi::scene::LightComponent::SPOT :
            kind == NativeLightKind::Rectangle ? wi::scene::LightComponent::RECTANGLE : wi::scene::LightComponent::POINT;
    }

    static XMVECTOR light_forward_axis(NativeLightKind kind) {
        return kind == NativeLightKind::Point ? XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f) :
            kind == NativeLightKind::Rectangle ? XMVectorSet(0.0f, 0.0f, -1.0f, 0.0f) :
                XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    }

    static XMVECTOR direction_rotation(const XMFLOAT3& direction, FXMVECTOR local_axis) {
        const XMVECTOR normalized_direction = XMVector3Normalize(XMLoadFloat3(&direction));
        const XMVECTOR axis = XMVector3Cross(local_axis, normalized_direction);
        const float dot = std::clamp(XMVectorGetX(XMVector3Dot(local_axis, normalized_direction)), -1.0f, 1.0f);
        if (dot < -0.999999f) {
            return XMQuaternionRotationAxis(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), XM_PI);
        }
        if (dot < 0.999999f) {
            return XMQuaternionRotationAxis(XMVector3Normalize(axis), std::acos(dot));
        }
        return XMQuaternionIdentity();
    }

    void apply(wi::ecs::Entity entity, const NativeLightDesc& desc) {
        auto* light = scene_.lights.GetComponent(entity);
        if (light == nullptr) return;
        auto* transform = scene_.transforms.GetComponent(entity);
        if (transform != nullptr) {
            transform->translation_local = desc.position;
            if (desc.kind != NativeLightKind::Point) {
                XMStoreFloat4(&transform->rotation_local, direction_rotation(desc.direction, light_forward_axis(desc.kind)));
            }
            transform->SetDirty();
            transform->UpdateTransform();
        }
        light->color = desc.color;
        light->position = desc.position;
        if (desc.kind == NativeLightKind::Point) {
            light->direction = XMFLOAT3(0.0f, -1.0f, 0.0f);
        } else {
            XMStoreFloat3(&light->direction, XMVector3Normalize(XMLoadFloat3(&desc.direction)));
        }
        light->intensity = desc.intensity;
        light->range = desc.range;
        light->outerConeAngle = desc.kind == NativeLightKind::Spot ? desc.outer_cone : 0.0f;
        light->innerConeAngle = desc.kind == NativeLightKind::Spot ? desc.inner_cone : 0.0f;
        light->length = desc.kind == NativeLightKind::Rectangle ? desc.rectangle_width : 0.0f;
        light->height = desc.kind == NativeLightKind::Rectangle ? desc.rectangle_height : 0.0f;
        light->SetType(light_type(desc.kind));
        light->SetCastShadow(desc.cast_shadow);
        light->forced_shadow_resolution = desc.shadow_resolution == 0 ? -1 : desc.shadow_resolution;
        light->SetVolumetricsEnabled(desc.volumetrics_enabled);
        light->volumetric_boost = desc.volumetric_boost;
    }

    uint32_t free_light() const { for (uint32_t i = 0; i < MAX_LIGHTS; ++i) if (!lights_[i].live) return i; return MAX_LIGHTS; }
    uint32_t free_environment() const { for (uint32_t i = 0; i < MAX_ENVIRONMENTS; ++i) if (!environments_[i].live) return i; return MAX_ENVIRONMENTS; }

    wi::scene::Scene& scene_;
    uintptr_t owner_;
    std::array<LightState, MAX_LIGHTS> lights_{};
    std::array<EnvironmentState, MAX_ENVIRONMENTS> environments_{};
};

inline bool probe_lighting_bridge(wi::scene::Scene& scene) {
    const size_t lights_before = scene.lights.GetCount();
    const auto original_sun_color = scene.weather.sunColor;
    const auto original_sun_direction = scene.weather.sunDirection;
    const auto original_ambient = scene.weather.ambient;
    const auto original_horizon = scene.weather.horizon;
    const float original_sky_exposure = scene.weather.skyExposure;
    const float original_fog_start = scene.weather.fogStart;
    const float original_fog_density = scene.weather.fogDensity;
    const bool original_height_fog = scene.weather.IsHeightFog();
    const bool original_override_fog = scene.weather.IsOverrideFogColor();
    LightingBridge bridge(scene);
    NativeLightDesc point;
    point.color = XMFLOAT3(0.3f, 0.7f, 1.0f);
    point.position = XMFLOAT3(1.0f, 2.0f, 3.0f);
    point.cast_shadow = true;
    const auto point_handle = bridge.create_light(point);
    NativeLightDesc spot = point;
    spot.kind = NativeLightKind::Spot;
    spot.direction = XMFLOAT3(0.0f, -1.0f, 0.0f);
    spot.position = XMFLOAT3(-4.0f, 1.0f, 2.0f);
    spot.outer_cone = 0.8f;
    const auto spot_handle = bridge.create_light(spot);
    const auto environment = bridge.create_environment(64, 50.0f, true);
    NativeEnvironmentDesc weather;
    weather.sun_color = XMFLOAT3(1.0f, 0.9f, 0.8f);
    weather.sun_direction = XMFLOAT3(-0.2f, -1.0f, 0.1f);
    weather.ambient = XMFLOAT3(0.15f, 0.2f, 0.25f);
    weather.fog_color = XMFLOAT3(0.4f, 0.5f, 0.6f);
    weather.sky_exposure = 1.25f;
    weather.fog_start = 12.0f;
    weather.fog_density = 0.02f;
    weather.fog_enabled = true;
    NativeEnvironmentDesc invalid_weather = weather;
    invalid_weather.sun_direction = XMFLOAT3(0.0f, 0.0f, 0.0f);
    if (!check(bridge.live(point_handle) && bridge.live(spot_handle) && bridge.live(environment) &&
        scene.lights.GetCount() == lights_before + 2, "lighting creates typed resources") ||
        !check(bridge.update_light(point_handle, spot) && !bridge.update_light(
            NativeLightHandle{point_handle.slot, point_handle.generation, 0}, point),
            "lighting moves and rejects foreign handle") ||
        !check(bridge.light_matches(point_handle, spot), "lighting updates Wicked transform and direction") ||
        !check(bridge.apply_environment(weather) && !bridge.apply_environment(invalid_weather) &&
            scene.weather.skyExposure == 1.25f && scene.weather.fogDensity == 0.02f && scene.weather.IsHeightFog(),
            "lighting applies validated sky and fog policy") ||
        !check(!bridge.create_environment(63, 1.0f, false).owner, "lighting rejects invalid environment")) return false;
    if (!check(bridge.destroy_light(spot_handle) && !bridge.live(spot_handle) &&
        bridge.destroy_light(point_handle) && bridge.destroy_environment(environment) &&
        scene.lights.GetCount() == lights_before, "lighting unloads resources")) return false;
    scene.weather.sunColor = original_sun_color;
    scene.weather.sunDirection = original_sun_direction;
    scene.weather.ambient = original_ambient;
    scene.weather.horizon = original_horizon;
    scene.weather.skyExposure = original_sky_exposure;
    scene.weather.fogStart = original_fog_start;
    scene.weather.fogDensity = original_fog_density;
    scene.weather.SetHeightFog(original_height_fog);
    scene.weather.SetOverrideFogColor(original_override_fog);
    return true;
}

} // namespace probe
