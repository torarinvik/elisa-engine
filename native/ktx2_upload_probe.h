#pragma once

#include "ktx2_upload.h"

#include <filesystem>

namespace probe {

inline bool check_ktx2_format_policy() {
    const KTX2UploadFormats all{true, true, true, true};
    return check(choose_ktx2_upload_encoding(false, all) == KTX2UploadEncoding::Bc1,
            "opaque KTX2 prefers BC1") &&
        check(choose_ktx2_upload_encoding(true, all) == KTX2UploadEncoding::Bc7,
            "alpha KTX2 prefers BC7") &&
        check(choose_ktx2_upload_encoding(true, {true, false, true, false}) == KTX2UploadEncoding::Bc3,
            "alpha KTX2 falls back to BC3") &&
        check(choose_ktx2_upload_encoding(true, {true, true, false, false}) == KTX2UploadEncoding::Rgba8,
            "alpha KTX2 avoids BC1") &&
        check(choose_ktx2_upload_encoding(false, {false, false, false, true}) == KTX2UploadEncoding::Bc7,
            "opaque KTX2 falls back to BC7") &&
        check(choose_ktx2_upload_encoding(false, all, KTX2TextureUsage::NormalData) == KTX2UploadEncoding::Rgba8,
            "normal data stays in channel-preserving RGBA8") &&
        check(choose_ktx2_upload_encoding(true, {}) == KTX2UploadEncoding::Unsupported,
            "KTX2 rejects missing safe fallback");
}

inline bool check_ktx2_cubemap_upload(const std::filesystem::path& path) {
    if (!check(std::filesystem::is_regular_file(path), "KTX2 cubemap artifact present")) return false;
    const wi::Resource resource = load_ktx2_texture_resource(path.lexically_normal().string());
    if (!check(resource.IsValid() && resource.GetTexture().IsValid(),
        "KTX2 cubemap uploaded to Wicked GPU")) return false;
    const wi::graphics::TextureDesc& desc = resource.GetTexture().GetDesc();
    const KTX2UploadFormats supported = query_ktx2_upload_formats(wi::graphics::GetDevice(), false);
    return check(desc.array_size == 6 && desc.width == desc.height &&
        desc.misc_flags == wi::graphics::ResourceMiscFlag::TEXTURECUBE &&
        desc.format == ktx2_wicked_format(choose_ktx2_upload_encoding(false, supported), false),
        "KTX2 upload preserves cubemap shape");
}

inline bool check_ktx2_alpha_upload(const std::filesystem::path& path) {
    if (!check(std::filesystem::is_regular_file(path), "KTX2 alpha artifact present")) return false;
    const wi::Resource resource = load_ktx2_texture_resource(path.lexically_normal().string());
    if (!check(resource.IsValid() && resource.GetTexture().IsValid(),
        "KTX2 alpha texture uploaded to Wicked GPU")) return false;
    const KTX2UploadFormats supported = query_ktx2_upload_formats(wi::graphics::GetDevice(), true);
    const KTX2UploadEncoding expected = choose_ktx2_upload_encoding(true, supported);
    return check(resource.GetTexture().GetDesc().format == ktx2_wicked_format(expected, true),
        "KTX2 alpha upload uses selected sRGB alpha-safe format");
}

inline bool check_ktx2_color_upload(const std::filesystem::path& path) {
    if (!check(std::filesystem::is_regular_file(path), "KTX2 color artifact present")) return false;
    const wi::Resource resource = load_ktx2_texture_resource(path.lexically_normal().string());
    if (!check(resource.IsValid() && resource.GetTexture().IsValid(),
        "KTX2 color texture uploaded to Wicked GPU")) return false;
    const KTX2UploadFormats supported = query_ktx2_upload_formats(wi::graphics::GetDevice(), false);
    const KTX2UploadEncoding expected = choose_ktx2_upload_encoding(false, supported);
    return check(resource.GetTexture().GetDesc().format == ktx2_wicked_format(expected, false),
        "KTX2 opaque upload uses selected native format");
}

inline bool check_ktx2_normal_data_upload(const std::filesystem::path& path) {
    const wi::Resource resource = load_ktx2_texture_resource(
        path.lexically_normal().string(), KTX2TextureUsage::NormalData);
    return check(resource.IsValid() && resource.GetTexture().IsValid() &&
        resource.GetTexture().GetDesc().format == wi::graphics::Format::R8G8B8A8_UNORM,
        "KTX2 normal data retains uncompressed linear channels");
}

inline bool check_ktx2_upload_fixtures(const std::filesystem::path& cooked_directory) {
    return check_ktx2_format_policy() &&
        check_ktx2_color_upload(cooked_directory / "maze_tile_tex.ktx2") &&
        check_ktx2_normal_data_upload(cooked_directory / "maze_tile_tex.ktx2") &&
        check_ktx2_cubemap_upload(cooked_directory / "maze_tile_cube.ktx2") &&
        check_ktx2_alpha_upload(cooked_directory / "maze_tile_alpha.ktx2");
}

inline int run_ktx2_upload_smoke(const std::filesystem::path& scene_manifest) {
    const std::filesystem::path project_root = scene_manifest.parent_path().parent_path();
    return check_ktx2_upload_fixtures(project_root / "build/cooked") ? 0 : 1;
}

} // namespace probe
