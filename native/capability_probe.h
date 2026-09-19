#pragma once

#include "probe_core.h"
#include "wiGraphicsDevice.h"
#include "wiRenderer.h"

#include <cstdio>

namespace probe {

inline bool probe_graphics_capabilities() {
    wi::graphics::GraphicsDevice* device = wi::graphics::GetDevice();
    if (!check(device != nullptr, "graphics capability device")) {
        return false;
    }
    const auto memory = device->GetMemoryUsage();
    const bool mesh_shader = device->CheckCapability(wi::graphics::GraphicsDeviceCapability::MESH_SHADER);
    const bool raytracing = device->CheckCapability(wi::graphics::GraphicsDeviceCapability::RAYTRACING);
    const bool sparse = device->CheckCapability(wi::graphics::GraphicsDeviceCapability::SPARSE_TEXTURE2D);
    const uint32_t viewport_count = device->GetMaxViewportCount();
    std::fprintf(stdout,
        "graphics capabilities: adapter=%s shader_format=%d mesh=%d raytracing=%d sparse=%d viewports=%u memory=%llu/%llu\n",
        device->GetAdapterName().c_str(), (int)device->GetShaderFormat(),
        mesh_shader ? 1 : 0, raytracing ? 1 : 0, sparse ? 1 : 0,
        viewport_count, (unsigned long long)memory.usage, (unsigned long long)memory.budget);
    if (!check(viewport_count > 0, "graphics viewport capability")) {
        return false;
    }
    // Unsupported optional features remain an explicit fallback decision. A
    // device without ray tracing or sparse textures still passes the base
    // renderer contract, but the report makes that choice visible to Elisa.
    std::fprintf(stdout, "graphics fallback: raytracing=%s sparse_textures=%s\n",
        raytracing ? "native" : "fallback", sparse ? "native" : "fallback");
    return true;
}

} // namespace probe
