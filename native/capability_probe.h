#pragma once

#include "capability_abi.h"
#include "probe_core.h"
#include "wiGraphicsDevice.h"
#include "wiJobSystem.h"
#include "wiRenderer.h"
#include "texture_format_policy.h"

#include <cstdio>
#include <cstdlib>

namespace probe {

inline bool probe_graphics_capabilities() {
    wi::graphics::GraphicsDevice* device = wi::graphics::GetDevice();
    if (!check(device != nullptr, "graphics capability device")) {
        return false;
    }
    const auto memory = device->GetMemoryUsage();
    const bool mesh_shader = device->CheckCapability(wi::graphics::GraphicsDeviceCapability::MESH_SHADER);
    const bool raytracing_native = device->CheckCapability(wi::graphics::GraphicsDeviceCapability::RAYTRACING);
    const bool sparse_native = device->CheckCapability(wi::graphics::GraphicsDeviceCapability::SPARSE_TEXTURE2D);
    const bool force_fallback = std::getenv("ELISA_FORCE_OPTIONAL_FALLBACK") != nullptr;
    const bool raytracing = raytracing_native && !force_fallback;
    const bool sparse = sparse_native && !force_fallback;
    const uint32_t viewport_count = device->GetMaxViewportCount();
    const auto format_supported = [device](wi::graphics::Format format) {
        wi::graphics::TextureDesc desc;
        desc.width = 4;
        desc.height = 4;
        desc.depth = 1;
        desc.array_size = 1;
        desc.mip_levels = 1;
        desc.sample_count = 1;
        desc.format = format;
        desc.bind_flags = wi::graphics::BindFlag::SHADER_RESOURCE;
        wi::graphics::Texture texture;
        const bool supported = device->CreateTexture(&desc, nullptr, &texture) && texture.IsValid();
        texture = {};
        return supported;
    };
    const uint64_t resource_formats =
        (format_supported(wi::graphics::Format::R8G8B8A8_UNORM) ? ELISA_FORMAT_RGBA8 : 0ull) |
        (format_supported(wi::graphics::Format::BC1_UNORM) ? ELISA_FORMAT_BC1 : 0ull) |
        (format_supported(wi::graphics::Format::R16_FLOAT) ? ELISA_FORMAT_R16_FLOAT : 0ull);
    const uint32_t graphics_workers = wi::jobsystem::GetThreadCount(wi::jobsystem::Priority::High);
    const uint32_t streaming_workers = wi::jobsystem::GetThreadCount(wi::jobsystem::Priority::Streaming);
    const ElisaBackendProfile profile = {
        sizeof(ElisaBackendProfile), ELISA_CAPABILITY_ABI_VERSION,
        ELISA_CAPABILITY_RENDERING | ELISA_CAPABILITY_NATIVE_WINDOW |
            ELISA_CAPABILITY_ASYNC_UPLOAD | ELISA_CAPABILITY_ASSET_LOADING,
        (raytracing ? ELISA_OPTIONAL_RAYTRACING : 0ull) |
            (sparse ? ELISA_OPTIONAL_SPARSE_TEXTURES : 0ull) |
            (mesh_shader ? ELISA_OPTIONAL_MESH_SHADERS : 0ull),
        viewport_count, graphics_workers, static_cast<uint64_t>(memory.budget),
        static_cast<uint64_t>(memory.usage), resource_formats, graphics_workers, streaming_workers,
    };
    if (!check(elisa_validate_backend_profile(&profile) == ELISA_CAPABILITY_OK,
            "versioned capability profile")) {
        return false;
    }
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
    if (!check(choose_texture_encoding(resource_formats, TextureEncoding::Bc1, false) ==
            TextureEncoding::Bc1, "BC1 format selection") ||
        !check(choose_texture_encoding(resource_formats, TextureEncoding::Bc1, true) ==
            TextureEncoding::Rgba8, "normal map BC1 fallback") ||
        !check(choose_texture_encoding(resource_formats, TextureEncoding::Bc1, false, true) ==
            TextureEncoding::Rgba8, "alpha texture BC1 fallback") ||
        !check(choose_texture_encoding(resource_formats, TextureEncoding::R16Float, true) ==
            TextureEncoding::Rgba8, "normal map scalar fallback") ||
        !check(choose_texture_encoding(resource_formats & ~ELISA_FORMAT_BC1,
            TextureEncoding::Bc1, false) == TextureEncoding::Rgba8,
            "BC1 unavailable fallback") ||
        !check(choose_texture_encoding(ELISA_FORMAT_R16_FLOAT, TextureEncoding::Bc1, false) ==
            TextureEncoding::Unsupported, "missing RGBA8 rejection")) {
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
    ElisaBackendProfile bad_version = profile;
    bad_version.abi_version += 1;
    return check(elisa_validate_backend_profile(&bad_version) == ELISA_CAPABILITY_UNSUPPORTED_VERSION,
        "capability version mismatch rejection");
}

} // namespace probe
