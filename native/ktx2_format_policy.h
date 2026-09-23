#pragma once

#include <cstdint>

namespace elisa::rendering::textures {

enum class KTX2UploadEncoding { Rgba8, Rgba16Float, Bc1, Bc3, Bc5, Bc6h, Bc7, Unsupported };
enum class KTX2TextureUsage { Color, NormalData };

struct KTX2UploadFormats {
    bool rgba8 = false;
    bool bc1 = false;
    bool bc3 = false;
    bool bc7 = false;
    bool bc5 = false;
    bool bc6h = false;
    bool rgba16f = false;
};

inline bool ktx2_upload_shape_supported(std::uint32_t layers, std::uint32_t faces,
    std::uint32_t width, std::uint32_t height) {
    return width > 0 && height > 0 && layers == 0 && (faces == 1 || faces == 6) &&
        (faces != 6 || width == height);
}

inline KTX2UploadEncoding choose_ktx2_upload_encoding(bool has_alpha, KTX2UploadFormats formats,
    KTX2TextureUsage usage = KTX2TextureUsage::Color, bool hdr = false) {
    if (hdr) {
        if (usage != KTX2TextureUsage::Color) return KTX2UploadEncoding::Unsupported;
        if (!has_alpha && formats.bc6h) return KTX2UploadEncoding::Bc6h;
        return formats.rgba16f ? KTX2UploadEncoding::Rgba16Float : KTX2UploadEncoding::Unsupported;
    }
    if (usage == KTX2TextureUsage::NormalData) {
        if (formats.bc5) return KTX2UploadEncoding::Bc5;
        return formats.rgba8 ? KTX2UploadEncoding::Rgba8 : KTX2UploadEncoding::Unsupported;
    }
    if (has_alpha) {
        if (formats.bc7) return KTX2UploadEncoding::Bc7;
        if (formats.bc3) return KTX2UploadEncoding::Bc3;
    } else {
        if (formats.bc1) return KTX2UploadEncoding::Bc1;
        if (formats.bc7) return KTX2UploadEncoding::Bc7;
        if (formats.bc3) return KTX2UploadEncoding::Bc3;
    }
    return formats.rgba8 ? KTX2UploadEncoding::Rgba8 : KTX2UploadEncoding::Unsupported;
}

} // namespace elisa::rendering::textures
