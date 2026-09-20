#pragma once

// Queries services and limits owned by the initialized SDL3/Wicked host.
#include "capability_abi.h"
#include "wiGraphicsDevice.h"
#include "wiJobSystem.h"

#include <algorithm>
#include <cstdlib>
#include <cstdint>

namespace probe {

inline bool query_live_backend_profile(ElisaBackendProfile& profile) {
    wi::graphics::GraphicsDevice* device = wi::graphics::GetDevice();
    if (device == nullptr) return false;

    const auto memory = device->GetMemoryUsage();
    const bool force_fallback = std::getenv("ELISA_FORCE_OPTIONAL_FALLBACK") != nullptr;
    const bool raytracing = !force_fallback &&
        device->CheckCapability(wi::graphics::GraphicsDeviceCapability::RAYTRACING);
    const bool sparse_textures = !force_fallback &&
        device->CheckCapability(wi::graphics::GraphicsDeviceCapability::SPARSE_TEXTURE2D);
    const bool mesh_shaders = device->CheckCapability(wi::graphics::GraphicsDeviceCapability::MESH_SHADER);
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
    const uint64_t formats =
        (format_supported(wi::graphics::Format::R8G8B8A8_UNORM) ? ELISA_FORMAT_RGBA8 : 0ull) |
        (format_supported(wi::graphics::Format::BC1_UNORM) ? ELISA_FORMAT_BC1 : 0ull) |
        (format_supported(wi::graphics::Format::R16_FLOAT) ? ELISA_FORMAT_R16_FLOAT : 0ull);
    const uint32_t graphics_workers = wi::jobsystem::GetThreadCount(wi::jobsystem::Priority::High);
    const uint32_t streaming_workers = wi::jobsystem::GetThreadCount(wi::jobsystem::Priority::Streaming);
    const uint32_t workers = std::max(graphics_workers, streaming_workers);
    profile = ElisaBackendProfile{
        sizeof(ElisaBackendProfile), ELISA_CAPABILITY_ABI_VERSION,
        ELISA_CAPABILITY_INPUT | ELISA_CAPABILITY_RENDERING | ELISA_CAPABILITY_NATIVE_WINDOW |
            ELISA_CAPABILITY_ASYNC_UPLOAD | ELISA_CAPABILITY_ASSET_LOADING,
        (raytracing ? ELISA_OPTIONAL_RAYTRACING : 0ull) |
            (sparse_textures ? ELISA_OPTIONAL_SPARSE_TEXTURES : 0ull) |
            (mesh_shaders ? ELISA_OPTIONAL_MESH_SHADERS : 0ull),
        device->GetMaxViewportCount(), workers, static_cast<uint64_t>(memory.budget),
        static_cast<uint64_t>(memory.usage), formats, graphics_workers, streaming_workers,
    };
    return elisa_validate_backend_profile(&profile) == ELISA_CAPABILITY_OK;
}

} // namespace probe
