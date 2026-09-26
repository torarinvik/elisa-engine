#pragma once

// Synchronous Wicked adapter. Completed staging buffers can instead use the
// CPU-only rgba_png.h encoder directly, without issuing another GPU readback.
#include "wiHelper.h"
#include "wiGraphics.h"
#include "rgba_png.h"

namespace probe {

inline bool save_rgba_png(const wi::graphics::Texture& texture, const std::string& path) {
    if (!texture.IsValid()) return false;
    const auto desc = texture.GetDesc();
    if (desc.width == 0 || desc.height == 0 ||
        desc.width > elisa::capture::MAX_RGBA_BYTES / 4 ||
        desc.height > elisa::capture::MAX_RGBA_BYTES / (size_t(desc.width) * 4)) return false;
    wi::vector<uint8_t> raw;
    if (!wi::helper::saveTextureToMemoryFile(texture, "RAW", raw)) return false;
    return elisa::capture::save_rgba_png(raw.data(), raw.size(), desc.width, desc.height,
        size_t(desc.width) * 4, elisa::capture::PixelOrder::RGBA, path);
}

} // namespace probe
