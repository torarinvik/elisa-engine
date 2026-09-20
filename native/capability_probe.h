#pragma once

#include "capability_abi.h"
#include "backend_capability_query.h"
#include "libmaze.h"
#include "probe_core.h"
#include "wiGraphicsDevice.h"
#include "wiJobSystem.h"
#include "wiRenderer.h"
#include "texture_format_policy.h"

#include <cstdio>
#include <cstdlib>
#include <limits>

namespace probe {

inline const char* capability_feature_name(int32_t feature) {
    switch (feature) {
    case ELISA_BACKEND_FEATURE_NONE: return "none";
    case ELISA_BACKEND_FEATURE_INPUT: return "input";
    case ELISA_BACKEND_FEATURE_RENDERING: return "rendering";
    case ELISA_BACKEND_FEATURE_PHYSICS: return "physics";
    case ELISA_BACKEND_FEATURE_AUDIO: return "audio";
    case ELISA_BACKEND_FEATURE_NATIVE_WINDOW: return "native-window";
    case ELISA_BACKEND_FEATURE_ASYNC_UPLOAD: return "async-upload";
    case ELISA_BACKEND_FEATURE_CALLBACKS: return "callbacks";
    case ELISA_BACKEND_FEATURE_ASSET_LOADING: return "asset-loading";
    default: return "invalid-profile";
    }
}

inline int32_t configure_elisa_backend(const ElisaBackendProfile& profile) {
    if (profile.memory_budget_bytes > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) ||
        profile.memory_usage_bytes > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
        return -3;
    }
    return maze_backend_configure(
        static_cast<int64_t>(profile.capability_bits),
        static_cast<int64_t>(profile.optional_bits),
        profile.max_viewports,
        profile.max_workers,
        static_cast<int64_t>(profile.memory_budget_bytes),
        static_cast<int64_t>(profile.memory_usage_bytes),
        static_cast<int64_t>(profile.resource_format_bits),
        profile.graphics_workers,
        profile.streaming_workers,
        profile.abi_version);
}

inline bool probe_graphics_capabilities() {
    wi::graphics::GraphicsDevice* device = wi::graphics::GetDevice();
    if (!check(device != nullptr, "graphics capability device")) {
        return false;
    }
    const auto memory = device->GetMemoryUsage();
    ElisaBackendProfile profile{};
    if (!check(probe::query_live_backend_profile(profile), "queried native backend profile")) return false;
    const bool force_fallback = std::getenv("ELISA_FORCE_OPTIONAL_FALLBACK") != nullptr;
    const bool mesh_shader = (profile.optional_bits & ELISA_OPTIONAL_MESH_SHADERS) != 0;
    const bool raytracing = (profile.optional_bits & ELISA_OPTIONAL_RAYTRACING) != 0;
    const bool sparse = (profile.optional_bits & ELISA_OPTIONAL_SPARSE_TEXTURES) != 0;
    const uint32_t viewport_count = profile.max_viewports;
    const uint64_t resource_formats = profile.resource_format_bits;
    const uint32_t graphics_workers = wi::jobsystem::GetThreadCount(wi::jobsystem::Priority::High);
    const uint32_t streaming_workers = wi::jobsystem::GetThreadCount(wi::jobsystem::Priority::Streaming);
    const int32_t configured_profile = configure_elisa_backend(profile);
    const int32_t configured_status = maze_backend_status();
    if (configured_profile != 0 || configured_status != 0) {
        std::fprintf(stderr,
            "runtime capability profile: configure=%d status=%d missing=%s capabilities=0x%llx optional=0x%llx formats=0x%llx version=%u\n",
            configured_profile, configured_status, capability_feature_name(maze_backend_missing_feature()),
            static_cast<unsigned long long>(profile.capability_bits),
            static_cast<unsigned long long>(profile.optional_bits),
            static_cast<unsigned long long>(profile.resource_format_bits), profile.abi_version);
    }
    if (!check(configured_profile == 0 && configured_status == 0,
            "native capability profile reaches Elisa runtime")) return false;
    ElisaBackendProfile no_input_profile = profile;
    no_input_profile.capability_bits &= ~ELISA_CAPABILITY_INPUT;
    if (!check(configure_elisa_backend(no_input_profile) == 0 && maze_backend_status() == -2 &&
            maze_backend_missing_feature() == ELISA_BACKEND_FEATURE_INPUT &&
            maze_start() == -2 && maze_session_create() == 0,
            "Elisa runtime rejects startup and sessions without required input") ||
        !check(configure_elisa_backend(profile) == 0 && maze_backend_status() == 0,
            "Elisa runtime accepts restored host profile")) return false;
    ElisaBackendProfile no_rendering_profile = profile;
    no_rendering_profile.capability_bits &= ~ELISA_CAPABILITY_RENDERING;
    no_rendering_profile.optional_bits = 0;
    no_rendering_profile.resource_format_bits = 0;
    if (!check(configure_elisa_backend(no_rendering_profile) == 0 && maze_backend_status() == -2 &&
            maze_backend_missing_feature() == ELISA_BACKEND_FEATURE_RENDERING,
            "Elisa reports missing rendering requirement") ||
        !check(configure_elisa_backend(profile) == 0 && maze_backend_status() == 0 &&
            maze_backend_missing_feature() == ELISA_BACKEND_FEATURE_NONE,
            "Elisa clears missing-feature report after profile restore")) return false;
    ElisaBackendProfile malformed_runtime_profile = profile;
    malformed_runtime_profile.optional_bits |= 1ull << 40;
    if (!check(configure_elisa_backend(malformed_runtime_profile) == -3 && maze_backend_status() == 0 &&
            maze_backend_missing_feature() == ELISA_BACKEND_FEATURE_NONE,
            "invalid runtime profile is rejected without replacing the active profile")) return false;
    uint64_t reported_limit = 0;
    if (!check(elisa_backend_profile_supports_capability(&profile, ELISA_CAPABILITY_RENDERING) &&
            !elisa_backend_profile_supports_capability(&profile, ELISA_CAPABILITY_AUDIO) &&
            !elisa_backend_profile_supports_capability(&profile,
                ELISA_CAPABILITY_RENDERING | ELISA_CAPABILITY_NATIVE_WINDOW),
            "typed capability queries") ||
        !check(elisa_backend_profile_supports_format(&profile, ELISA_FORMAT_RGBA8) ==
            ((resource_formats & ELISA_FORMAT_RGBA8) != 0) &&
            !elisa_backend_profile_supports_format(&profile, ELISA_FORMAT_RGBA8 | ELISA_FORMAT_BC1),
            "typed texture format queries") ||
        !check(elisa_backend_profile_limit(&profile, ELISA_LIMIT_MEMORY_AVAILABLE_BYTES, &reported_limit) &&
            reported_limit == profile.memory_budget_bytes - profile.memory_usage_bytes &&
            elisa_backend_profile_limit(&profile, ELISA_LIMIT_STREAMING_WORKERS, &reported_limit) &&
            reported_limit == profile.streaming_workers,
            "typed limit queries")) return false;
    std::fprintf(stdout,
        "graphics capabilities: adapter=%s shader_format=%d mesh=%d raytracing=%d sparse=%d viewports=%u memory=%llu/%llu profile=0x%llx optional=0x%llx formats=0x%llx workers=%u/%u\n",
        device->GetAdapterName().c_str(), (int)device->GetShaderFormat(),
        mesh_shader ? 1 : 0, raytracing ? 1 : 0, sparse ? 1 : 0,
        viewport_count, (unsigned long long)memory.usage, (unsigned long long)memory.budget,
        (unsigned long long)profile.capability_bits, (unsigned long long)profile.optional_bits,
        (unsigned long long)profile.resource_format_bits, profile.graphics_workers, profile.streaming_workers);
    if (!check(viewport_count > 0, "graphics viewport capability")) {
        return false;
    }
    const uint64_t all_formats = ELISA_FORMAT_RGBA8 | ELISA_FORMAT_BC1 | ELISA_FORMAT_R16_FLOAT;
    if (!check(choose_texture_encoding(all_formats, TextureEncoding::Bc1, false) ==
            TextureEncoding::Bc1, "BC1 format selection") ||
        !check(choose_texture_encoding(all_formats, TextureEncoding::Bc1, true) ==
            TextureEncoding::Rgba8, "normal map BC1 fallback") ||
        !check(choose_texture_encoding(all_formats, TextureEncoding::Bc1, false, true) ==
            TextureEncoding::Rgba8, "alpha texture BC1 fallback") ||
        !check(choose_texture_encoding(all_formats, TextureEncoding::R16Float, true) ==
            TextureEncoding::Rgba8, "normal map scalar fallback") ||
        !check(choose_texture_encoding(ELISA_FORMAT_RGBA8 | ELISA_FORMAT_R16_FLOAT,
            TextureEncoding::Bc1, false) == TextureEncoding::Rgba8,
            "BC1 unavailable fallback") ||
        !check(choose_texture_encoding(ELISA_FORMAT_BC1, TextureEncoding::Bc1, false, true) ==
            TextureEncoding::Unsupported, "unsafe BC1 without RGBA8 rejection") ||
        !check(choose_texture_encoding(ELISA_FORMAT_BC1, TextureEncoding::Bc1, true) ==
            TextureEncoding::Unsupported, "normal map rejects without RGBA8 fallback") ||
        !check(choose_texture_encoding(ELISA_FORMAT_R16_FLOAT, TextureEncoding::Bc1, false) ==
            TextureEncoding::Unsupported, "missing RGBA8 rejection") ||
        !check(choose_texture_encoding(all_formats, TextureEncoding::Unsupported, false) ==
            TextureEncoding::Unsupported, "invalid requested texture format rejection") ||
        !check(((resource_formats & ELISA_FORMAT_RGBA8) != 0) ==
            (choose_texture_encoding(resource_formats, TextureEncoding::Rgba8, false) == TextureEncoding::Rgba8),
            "queried RGBA8 capability matches policy")) {
        return false;
    }
    // Unsupported optional features remain an explicit fallback decision. A
    // device without ray tracing or sparse textures still passes the base
    // renderer contract, but the report makes that choice visible to Elisa.
    std::fprintf(stdout, "graphics fallback: raytracing=%s sparse_textures=%s\n",
        raytracing ? "native" : "fallback", sparse ? "native" : "fallback");
    if (!check(!force_fallback || (!raytracing && !sparse), "optional fallback policy")) {
        return false;
    }
    ElisaBackendProfile future_profile = profile;
    future_profile.struct_size += sizeof(uint64_t);
    if (!check(elisa_validate_backend_profile(&future_profile) == ELISA_CAPABILITY_OK,
            "forward-sized capability profile")) return false;
    ElisaBackendProfile unknown_capability = profile;
    unknown_capability.capability_bits |= 1ull << 40;
    if (!check(elisa_validate_backend_profile(&unknown_capability) == ELISA_CAPABILITY_INVALID_ARGUMENT,
            "unknown capability bit rejection")) return false;
    ElisaBackendProfile unknown_format = profile;
    unknown_format.resource_format_bits |= 1ull << 40;
    if (!check(elisa_validate_backend_profile(&unknown_format) == ELISA_CAPABILITY_INVALID_ARGUMENT,
            "unknown format bit rejection")) return false;
    if (!check(!elisa_backend_profile_supports_capability(&unknown_capability, ELISA_CAPABILITY_RENDERING) &&
            !elisa_backend_profile_supports_format(&unknown_format, ELISA_FORMAT_RGBA8) &&
            !elisa_backend_profile_limit(&profile, static_cast<ElisaBackendLimit>(255), &reported_limit),
            "typed query rejects invalid profile or limit")) return false;
    ElisaBackendProfile optional_without_renderer = profile;
    optional_without_renderer.capability_bits &= ~ELISA_CAPABILITY_RENDERING;
    optional_without_renderer.optional_bits = ELISA_OPTIONAL_MESH_SHADERS;
    if (!check(elisa_validate_backend_profile(&optional_without_renderer) == ELISA_CAPABILITY_INVALID_ARGUMENT,
            "renderer feature dependency rejection")) return false;
    ElisaBackendProfile bad_version = profile;
    bad_version.abi_version += 1;
    return check(elisa_validate_backend_profile(&bad_version) == ELISA_CAPABILITY_UNSUPPORTED_VERSION,
        "capability version mismatch rejection");
}

} // namespace probe
