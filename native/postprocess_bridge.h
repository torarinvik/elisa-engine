#pragma once

#include "probe_core.h"
#include "wiRenderPath3D.h"
#include "wiEnums.h"
#include "wiRenderer.h"

#include <array>
#include <cmath>
#include <cstdint>

namespace probe {

enum class NativeTonemap : uint8_t { Reinhard, Aces, Uchimura };
enum class NativeUpscaler : uint8_t { None, Fsr1, Fsr2 };
enum class PostProcessApply : uint8_t { Applied, Fallback, Invalid };

struct UpscalerCapabilities {
    bool fsr1 = false;
    bool fsr2 = false;

    bool supports(NativeUpscaler upscaler) const {
        return upscaler == NativeUpscaler::None ||
            (upscaler == NativeUpscaler::Fsr1 && fsr1) ||
            (upscaler == NativeUpscaler::Fsr2 && fsr2);
    }
};

inline bool supports_storage_format(wi::graphics::GraphicsDevice& device,
    wi::graphics::Format format) {
    wi::graphics::TextureDesc desc;
    desc.width = 4;
    desc.height = 4;
    desc.depth = 1;
    desc.array_size = 1;
    desc.mip_levels = 1;
    desc.sample_count = 1;
    desc.format = format;
    desc.bind_flags = wi::graphics::BindFlag::SHADER_RESOURCE |
        wi::graphics::BindFlag::UNORDERED_ACCESS;
    wi::graphics::Texture texture;
    try {
        const bool supported = device.CreateTexture(&desc, nullptr, &texture) && texture.IsValid();
        texture = {};
        return supported;
    } catch (...) {
        texture = {};
        return false;
    }
}

inline bool shader_is_available(wi::enums::SHADERTYPE shader) {
    const wi::graphics::Shader* compiled = wi::renderer::GetShader(shader);
    return compiled != nullptr && compiled->IsValid();
}

inline UpscalerCapabilities query_upscaler_capabilities(wi::graphics::GraphicsDevice* device) {
    UpscalerCapabilities result;
    if (device == nullptr) return result;
    const bool main_target_supported = supports_storage_format(
        *device, wi::renderer::format_rendertarget_main);
    result.fsr1 = main_target_supported &&
        shader_is_available(wi::enums::CSTYPE_POSTPROCESS_FSR_UPSCALING) &&
        shader_is_available(wi::enums::CSTYPE_POSTPROCESS_FSR_SHARPEN);

    constexpr std::array<wi::graphics::Format, 11> fsr2_formats = {
        wi::graphics::Format::R16G16B16A16_UNORM,
        wi::graphics::Format::R32G32_FLOAT,
        wi::graphics::Format::R16_FLOAT,
        wi::graphics::Format::R8G8B8A8_UNORM,
        wi::graphics::Format::R32_UINT,
        wi::graphics::Format::R16G16_FLOAT,
        wi::graphics::Format::R8G8_UNORM,
        wi::graphics::Format::R8_UNORM,
        wi::graphics::Format::R11G11B10_FLOAT,
        wi::graphics::Format::R16G16B16A16_FLOAT,
        wi::graphics::Format::R16_SNORM,
    };
    bool fsr2_formats_supported = device->CheckCapability(
        wi::graphics::GraphicsDeviceCapability::UAV_LOAD_FORMAT_COMMON);
    for (wi::graphics::Format format : fsr2_formats) {
        fsr2_formats_supported = fsr2_formats_supported && supports_storage_format(*device, format);
    }
    constexpr std::array<wi::enums::SHADERTYPE, 8> fsr2_shaders = {
        wi::enums::CSTYPE_POSTPROCESS_FSR2_AUTOGEN_REACTIVE_PASS,
        wi::enums::CSTYPE_POSTPROCESS_FSR2_COMPUTE_LUMINANCE_PYRAMID_PASS,
        wi::enums::CSTYPE_POSTPROCESS_FSR2_PREPARE_INPUT_COLOR_PASS,
        wi::enums::CSTYPE_POSTPROCESS_FSR2_RECONSTRUCT_PREVIOUS_DEPTH_PASS,
        wi::enums::CSTYPE_POSTPROCESS_FSR2_DEPTH_CLIP_PASS,
        wi::enums::CSTYPE_POSTPROCESS_FSR2_LOCK_PASS,
        wi::enums::CSTYPE_POSTPROCESS_FSR2_ACCUMULATE_PASS,
        wi::enums::CSTYPE_POSTPROCESS_FSR2_RCAS_PASS,
    };
    bool fsr2_shaders_supported = true;
    for (wi::enums::SHADERTYPE shader : fsr2_shaders) {
        fsr2_shaders_supported = fsr2_shaders_supported && shader_is_available(shader);
    }
    result.fsr2 = fsr2_formats_supported && fsr2_shaders_supported;
    return result;
}

struct NativePostProcessDesc {
    NativeTonemap tonemap = NativeTonemap::Aces;
    NativeUpscaler upscaler = NativeUpscaler::None;
    float render_scale = 1.0f;
    float bloom_threshold = 1.0f;
    bool bloom = true;
    bool fxaa = false;
    bool temporal_aa = false;
    bool ambient_occlusion = false;
    bool screen_space_reflections = false;
    bool fog = true;
    bool depth_effects = true;
};

class PostProcessBridge {
public:
    explicit PostProcessBridge(wi::RenderPath3D& path) : path_(path) {}

    PostProcessApply apply(const NativePostProcessDesc& desc,
        const UpscalerCapabilities& upscalers) {
        if (!valid(desc)) return PostProcessApply::Invalid;
        path_.setTonemap(tonemap(desc.tonemap));
        path_.setBloomThreshold(desc.bloom_threshold);
        path_.setBloomEnabled(desc.bloom);
        path_.setFXAAEnabled(desc.fxaa);
        wi::renderer::SetTemporalAAEnabled(desc.temporal_aa);
        path_.setAO(desc.ambient_occlusion ? wi::RenderPath3D::AO_SSAO : wi::RenderPath3D::AO_DISABLED);
        path_.setSSREnabled(desc.screen_space_reflections);
        if (path_.scene != nullptr) path_.scene->weather.SetHeightFog(desc.fog);
        path_.setDepthOfFieldEnabled(desc.depth_effects);
        path_.resolutionScale = desc.render_scale;
        path_.setFSREnabled(false);
        path_.setFSR2Enabled(false);
        if (desc.upscaler == NativeUpscaler::Fsr1) {
            const XMUINT2 internal = path_.GetInternalResolution();
            if (!upscalers.supports(desc.upscaler) || desc.render_scale >= 1.0f ||
                path_.GetPhysicalWidth() == 0 || path_.GetPhysicalHeight() == 0 ||
                internal.x >= path_.GetPhysicalWidth() || internal.y >= path_.GetPhysicalHeight()) {
                return PostProcessApply::Fallback;
            }
            path_.setFSREnabled(true);
        }
        if (desc.upscaler == NativeUpscaler::Fsr2) {
            const XMUINT2 internal = path_.GetInternalResolution();
            if (!upscalers.supports(desc.upscaler) || internal.x < 2 || internal.y < 2 ||
                path_.GetPhysicalWidth() == 0 || path_.GetPhysicalHeight() == 0) {
                return PostProcessApply::Fallback;
            }
            path_.setFSR2Enabled(true);
        }
        return PostProcessApply::Applied;
    }

private:
    static bool valid(const NativePostProcessDesc& desc) {
        return std::isfinite(desc.render_scale) && desc.render_scale >= 0.25f &&
            desc.render_scale <= 1.0f && std::isfinite(desc.bloom_threshold) &&
            desc.bloom_threshold >= 0.0f && desc.bloom_threshold <= 100.0f &&
            !(desc.temporal_aa && desc.upscaler == NativeUpscaler::Fsr2);
    }

    static wi::renderer::Tonemap tonemap(NativeTonemap value) {
        return value == NativeTonemap::Reinhard ? wi::renderer::Tonemap::Reinhard :
            value == NativeTonemap::Uchimura ? wi::renderer::Tonemap::Uchimura :
            wi::renderer::Tonemap::ACES;
    }

    wi::RenderPath3D& path_;
};

inline bool probe_postprocess_bridge(wi::RenderPath3D& path) {
    PostProcessBridge bridge(path);
    const bool original_fog = path.scene != nullptr && path.scene->weather.IsHeightFog();
    NativePostProcessDesc desc;
    desc.tonemap = NativeTonemap::Uchimura;
    desc.upscaler = NativeUpscaler::Fsr1;
    desc.render_scale = 0.5f;
    desc.bloom_threshold = 2.0f;
    desc.fxaa = true;
    desc.temporal_aa = true;
    desc.ambient_occlusion = true;
    desc.screen_space_reflections = true;
    desc.fog = false;
    const UpscalerCapabilities unavailable{};
    const UpscalerCapabilities available{true, true};
    NativePostProcessDesc native_resolution;
    native_resolution.upscaler = NativeUpscaler::Fsr1;
    if (!check(!unavailable.supports(NativeUpscaler::Fsr1) &&
            !unavailable.supports(NativeUpscaler::Fsr2) &&
            available.supports(NativeUpscaler::Fsr1) &&
            available.supports(NativeUpscaler::Fsr2),
            "upscaler availability is tracked per backend path") ||
        !check(bridge.apply(desc, unavailable) == PostProcessApply::Fallback,
            "postprocess reports unsupported upscaler fallback") ||
        !check(path.getTonemap() == wi::renderer::Tonemap::Uchimura &&
            path.getBloomThreshold() == 2.0f && path.getFXAAEnabled() &&
            wi::renderer::GetTemporalAAEnabled() &&
            path.getAO() == wi::RenderPath3D::AO_SSAO && path.getSSREnabled() &&
            path.resolutionScale == 0.5f && !path.getFSREnabled() && !path.getFSR2Enabled() &&
            path.scene != nullptr && !path.scene->weather.IsHeightFog(),
            "postprocess applies validated profile") ||
        !check(bridge.apply(native_resolution, available) == PostProcessApply::Fallback &&
            !path.getFSREnabled() && !path.getFSR2Enabled(),
            "postprocess falls back when FSR1 has no upscale work") ||
        !check(bridge.apply(NativePostProcessDesc{}, available) == PostProcessApply::Applied &&
            path.getTonemap() == wi::renderer::Tonemap::ACES && path.resolutionScale == 1.0f &&
            !wi::renderer::GetTemporalAAEnabled() &&
            path.scene != nullptr && path.scene->weather.IsHeightFog(),
            "postprocess applies supported default")) {
        return false;
    }
    NativePostProcessDesc invalid;
    invalid.render_scale = 2.0f;
    const bool rejected = check(bridge.apply(invalid, available) == PostProcessApply::Invalid,
        "postprocess rejects invalid scale");
    invalid = NativePostProcessDesc{};
    invalid.upscaler = NativeUpscaler::Fsr2;
    invalid.temporal_aa = true;
    const bool rejects_double_temporal = check(
        bridge.apply(invalid, available) == PostProcessApply::Invalid,
        "postprocess rejects simultaneous TAA and FSR2");
    if (path.scene != nullptr) path.scene->weather.SetHeightFog(original_fog);
    return rejected && rejects_double_temporal;
}

} // namespace probe
