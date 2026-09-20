#pragma once

#include "probe_core.h"
#include "wiRenderPath3D.h"

#include <cmath>
#include <cstdint>

namespace probe {

enum class NativeTonemap : uint8_t { Reinhard, Aces, Uchimura };
enum class NativeUpscaler : uint8_t { None, Fsr1, Fsr2 };
enum class PostProcessApply : uint8_t { Applied, Fallback, Invalid };

struct NativePostProcessDesc {
    NativeTonemap tonemap = NativeTonemap::Aces;
    NativeUpscaler upscaler = NativeUpscaler::None;
    float render_scale = 1.0f;
    float bloom_threshold = 1.0f;
    bool bloom = true;
    bool fxaa = false;
    bool ambient_occlusion = false;
    bool screen_space_reflections = false;
    bool depth_effects = true;
};

class PostProcessBridge {
public:
    explicit PostProcessBridge(wi::RenderPath3D& path) : path_(path) {}

    PostProcessApply apply(const NativePostProcessDesc& desc, bool fsr_supported) {
        if (!valid(desc)) return PostProcessApply::Invalid;
        path_.setTonemap(tonemap(desc.tonemap));
        path_.setBloomThreshold(desc.bloom_threshold);
        path_.setBloomEnabled(desc.bloom);
        path_.setFXAAEnabled(desc.fxaa);
        path_.setAO(desc.ambient_occlusion ? wi::RenderPath3D::AO_SSAO : wi::RenderPath3D::AO_DISABLED);
        path_.setSSREnabled(desc.screen_space_reflections);
        path_.setDepthOfFieldEnabled(desc.depth_effects);
        path_.resolutionScale = desc.render_scale;
        path_.setFSREnabled(false);
        path_.setFSR2Enabled(false);
        if (desc.upscaler == NativeUpscaler::Fsr1) {
            if (!fsr_supported) return PostProcessApply::Fallback;
            path_.setFSREnabled(true);
        }
        if (desc.upscaler == NativeUpscaler::Fsr2) {
            if (!fsr_supported) return PostProcessApply::Fallback;
            path_.setFSR2Enabled(true);
        }
        return PostProcessApply::Applied;
    }

private:
    static bool valid(const NativePostProcessDesc& desc) {
        return std::isfinite(desc.render_scale) && desc.render_scale >= 0.25f &&
            desc.render_scale <= 1.0f && std::isfinite(desc.bloom_threshold) &&
            desc.bloom_threshold >= 0.0f && desc.bloom_threshold <= 100.0f;
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
    NativePostProcessDesc desc;
    desc.tonemap = NativeTonemap::Uchimura;
    desc.upscaler = NativeUpscaler::Fsr2;
    desc.render_scale = 0.5f;
    desc.bloom_threshold = 2.0f;
    desc.fxaa = true;
    desc.ambient_occlusion = true;
    desc.screen_space_reflections = true;
    if (!check(bridge.apply(desc, false) == PostProcessApply::Fallback,
        "postprocess reports unsupported upscaler fallback") ||
        !check(path.getTonemap() == wi::renderer::Tonemap::Uchimura &&
            path.getBloomThreshold() == 2.0f && path.getFXAAEnabled() &&
            path.getAO() == wi::RenderPath3D::AO_SSAO && path.getSSREnabled() &&
            path.resolutionScale == 0.5f, "postprocess applies validated profile") ||
        !check(bridge.apply(NativePostProcessDesc{}, true) == PostProcessApply::Applied &&
            path.getTonemap() == wi::renderer::Tonemap::ACES && path.resolutionScale == 1.0f,
            "postprocess applies supported default")) {
        return false;
    }
    NativePostProcessDesc invalid;
    invalid.render_scale = 2.0f;
    return check(bridge.apply(invalid, true) == PostProcessApply::Invalid,
        "postprocess rejects invalid scale");
}

} // namespace probe
