#pragma once

#include "capability_abi.h"

#include <cstdint>

namespace probe {

enum class TextureEncoding : uint8_t { Rgba8, Bc1, R16Float, Unsupported };

inline TextureEncoding choose_texture_encoding(uint64_t supported, TextureEncoding requested,
    bool normal_map) {
    if (requested == TextureEncoding::Bc1 && !normal_map && (supported & ELISA_FORMAT_BC1) != 0) {
        return TextureEncoding::Bc1;
    }
    if (requested == TextureEncoding::R16Float && (supported & ELISA_FORMAT_R16_FLOAT) != 0) {
        return TextureEncoding::R16Float;
    }
    if ((requested == TextureEncoding::Rgba8 || normal_map || requested == TextureEncoding::Bc1 ||
            requested == TextureEncoding::R16Float) && (supported & ELISA_FORMAT_RGBA8) != 0) {
        return TextureEncoding::Rgba8;
    }
    return TextureEncoding::Unsupported;
}

} // namespace probe
