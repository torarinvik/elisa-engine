#pragma once

#include "capability_abi.h"

#include <cstdint>

namespace probe {

enum class TextureEncoding : uint8_t { Rgba8, Bc1, R16Float, Unsupported };

inline TextureEncoding choose_texture_encoding(uint64_t supported, TextureEncoding requested,
    bool normal_map, bool has_alpha = false) {
    if (normal_map) {
        return (supported & ELISA_FORMAT_RGBA8) != 0 ? TextureEncoding::Rgba8 : TextureEncoding::Unsupported;
    }
    if (requested == TextureEncoding::Bc1 && !has_alpha && (supported & ELISA_FORMAT_BC1) != 0) {
        return TextureEncoding::Bc1;
    }
    if (requested == TextureEncoding::R16Float && (supported & ELISA_FORMAT_R16_FLOAT) != 0) {
        return TextureEncoding::R16Float;
    }
    if ((requested == TextureEncoding::Rgba8 || requested == TextureEncoding::Bc1 ||
            requested == TextureEncoding::R16Float) && (supported & ELISA_FORMAT_RGBA8) != 0) {
        return TextureEncoding::Rgba8;
    }
    return TextureEncoding::Unsupported;
}

} // namespace probe
