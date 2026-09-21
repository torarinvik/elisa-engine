#pragma once

#include "wiRenderPath3D.h"
#include "wiScene.h"

#include <cmath>

namespace elisa::render_scene_effects {

constexpr float MAX_EMISSIVE_STRENGTH = 64.0f;
constexpr float MAX_BLOOM_THRESHOLD = 64.0f;
constexpr float MIN_AO_RANGE = 0.01f;
constexpr float MAX_AO_RANGE = 32.0f;
constexpr float MIN_AO_POWER = 0.0f;
constexpr float MAX_AO_POWER = 8.0f;

inline bool valid_emission(float red, float green, float blue, float strength) {
    return std::isfinite(red) && std::isfinite(green) && std::isfinite(blue) && std::isfinite(strength) &&
        red >= 0.0f && red <= 1.0f && green >= 0.0f && green <= 1.0f &&
        blue >= 0.0f && blue <= 1.0f && strength >= 0.0f && strength <= MAX_EMISSIVE_STRENGTH;
}

inline void apply_emission(wi::scene::MaterialComponent& material,
    float red, float green, float blue, float strength) {
    material.shaderType = wi::scene::MaterialComponent::SHADERTYPE_PBR;
    material.SetEmissiveColor(XMFLOAT4(red, green, blue, strength));
}

inline void apply_bloom(wi::RenderPath3D& path, bool enabled, float threshold) {
    path.setBloomThreshold(threshold);
    path.setBloomEnabled(enabled);
}

inline void apply_ambient_occlusion(wi::RenderPath3D& path, bool enabled) {
    path.setAO(enabled ? wi::RenderPath3D::AO_SSAO : wi::RenderPath3D::AO_DISABLED);
}

inline bool valid_ambient_occlusion_settings(float range, float power) {
    return std::isfinite(range) && std::isfinite(power) &&
        range >= MIN_AO_RANGE && range <= MAX_AO_RANGE &&
        power >= MIN_AO_POWER && power <= MAX_AO_POWER;
}

inline void apply_ambient_occlusion_settings(wi::RenderPath3D& path,
    float range, float power) {
    path.setAORange(range);
    path.setAOPower(power);
}

inline void apply_fxaa(wi::RenderPath3D& path, bool enabled) {
    path.setFXAAEnabled(enabled);
}

} // namespace elisa::render_scene_effects
