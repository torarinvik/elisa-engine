#pragma once

#include "capability_abi.h"
#include "probe_core.h"
#include "wiGraphicsDevice.h"
#include "wiRenderer.h"

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
    const ElisaBackendProfile profile = {
        sizeof(ElisaBackendProfile), ELISA_CAPABILITY_ABI_VERSION,
        ELISA_CAPABILITY_RENDERING | ELISA_CAPABILITY_NATIVE_WINDOW |
            ELISA_CAPABILITY_ASYNC_UPLOAD | ELISA_CAPABILITY_ASSET_LOADING,
        (raytracing ? ELISA_OPTIONAL_RAYTRACING : 0ull) |
            (sparse ? ELISA_OPTIONAL_SPARSE_TEXTURES : 0ull) |
            (mesh_shader ? ELISA_OPTIONAL_MESH_SHADERS : 0ull),
        viewport_count, 1u, static_cast<uint64_t>(memory.budget),
        static_cast<uint64_t>(memory.usage),
    };
    if (!check(elisa_validate_backend_profile(&profile) == ELISA_CAPABILITY_OK,
            "versioned capability profile")) {
        return false;
    }
    std::fprintf(stdout,
        "graphics capabilities: adapter=%s shader_format=%d mesh=%d raytracing=%d sparse=%d viewports=%u memory=%llu/%llu profile=0x%llx optional=0x%llx\n",
        device->GetAdapterName().c_str(), (int)device->GetShaderFormat(),
        mesh_shader ? 1 : 0, raytracing ? 1 : 0, sparse ? 1 : 0,
        viewport_count, (unsigned long long)memory.usage, (unsigned long long)memory.budget,
        (unsigned long long)profile.capability_bits, (unsigned long long)profile.optional_bits);
    if (!check(viewport_count > 0, "graphics viewport capability")) {
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
