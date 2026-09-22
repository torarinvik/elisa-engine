#pragma once

namespace elisa::rendering::textures {

enum class KTX2UploadEncoding { Rgba8, Bc1, Bc3, Bc5, Bc7, Unsupported };
enum class KTX2TextureUsage { Color, NormalData };

struct KTX2UploadFormats {
    bool rgba8 = false;
    bool bc1 = false;
    bool bc3 = false;
    bool bc7 = false;
    bool bc5 = false;
};

inline KTX2UploadEncoding choose_ktx2_upload_encoding(bool has_alpha, KTX2UploadFormats formats,
    KTX2TextureUsage usage = KTX2TextureUsage::Color) {
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
