#pragma once

#include "ktx2_upload.h"
#include "wiHelper.h"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace probe {

using elisa::rendering::textures::KTX2TextureUsage;
using elisa::rendering::textures::KTX2UploadEncoding;
using elisa::rendering::textures::KTX2UploadFormats;
using elisa::rendering::textures::MAX_KTX2_CONTAINER_BYTES;
using elisa::rendering::textures::choose_ktx2_upload_encoding;
using elisa::rendering::textures::ktx2_wicked_format;
using elisa::rendering::textures::load_ktx2_texture_resource;
using elisa::rendering::textures::query_ktx2_upload_formats;
using elisa::rendering::textures::read_bounded_ktx2_container;

inline bool check_ktx2_bounded_reader(const std::filesystem::path& directory) {
    const std::filesystem::path fixture = directory / "ktx2-reader-probe.tmp";
    std::error_code error;
    std::filesystem::remove(fixture, error);
    {
        std::ofstream output(fixture, std::ios::binary | std::ios::trunc);
        const char sample[] = { '\x13', '\x27', '\x39', '\x4B' };
        output.write(sample, sizeof(sample));
    }
    std::vector<uint8_t> bytes;
    const bool exact_file_read = read_bounded_ktx2_container(fixture.string(), bytes) &&
        bytes.size() == 4 && bytes[0] == 0x13 && bytes[3] == 0x4B;
    {
        std::ofstream output(fixture, std::ios::binary | std::ios::trunc);
    }
    const bool empty_file_rejected = !read_bounded_ktx2_container(fixture.string(), bytes) && bytes.empty();
    error.clear();
    std::filesystem::resize_file(fixture, MAX_KTX2_CONTAINER_BYTES + 1, error);
    const bool oversized_file_rejected = !error &&
        !read_bounded_ktx2_container(fixture.string(), bytes) && bytes.empty();
    error.clear();
    std::filesystem::remove(fixture, error);
    return check(exact_file_read, "KTX2 reader accepts an exact small file") &&
        check(empty_file_rejected, "KTX2 reader rejects empty files") &&
        check(oversized_file_rejected, "KTX2 reader rejects oversized files before allocation");
}

inline bool check_ktx2_format_policy() {
    const KTX2UploadFormats all{true, true, true, true, true, true, true};
    const KTX2UploadFormats hdr{false, false, false, false, false, true, true};
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
        check(choose_ktx2_upload_encoding(false, all, KTX2TextureUsage::NormalData) == KTX2UploadEncoding::Bc5,
            "normal data prefers two-channel BC5") &&
        check(choose_ktx2_upload_encoding(false, {true, false, false, false, false},
                KTX2TextureUsage::NormalData) == KTX2UploadEncoding::Rgba8,
            "normal data falls back to channel-preserving RGBA8") &&
        check(choose_ktx2_upload_encoding(false, {false, false, false, false, false},
                KTX2TextureUsage::NormalData) == KTX2UploadEncoding::Unsupported,
            "normal data rejects missing safe formats") &&
        check(choose_ktx2_upload_encoding(true, {}) == KTX2UploadEncoding::Unsupported,
            "KTX2 rejects missing safe fallback") &&
        check(choose_ktx2_upload_encoding(false, hdr, KTX2TextureUsage::Color, true) ==
                KTX2UploadEncoding::Bc6h,
            "HDR KTX2 prefers BC6H without losing range") &&
        check(choose_ktx2_upload_encoding(true, hdr, KTX2TextureUsage::Color, true) ==
                KTX2UploadEncoding::Rgba16Float,
            "HDR alpha data falls back to half-float RGBA") &&
        check(choose_ktx2_upload_encoding(false, {false, false, false, false, false, false, true},
                KTX2TextureUsage::Color, true) == KTX2UploadEncoding::Rgba16Float,
            "HDR KTX2 uses a half-float fallback without BC6H") &&
        check(choose_ktx2_upload_encoding(false, {true, false, false, false, false},
                KTX2TextureUsage::Color, true) == KTX2UploadEncoding::Unsupported,
            "HDR KTX2 never falls back to range-losing RGBA8") &&
        check(choose_ktx2_upload_encoding(false, hdr, KTX2TextureUsage::NormalData, true) ==
                KTX2UploadEncoding::Unsupported,
            "HDR color encoding is not reused for normal data") &&
        check(ktx2_wicked_format(KTX2UploadEncoding::Bc6h, false) ==
                wi::graphics::Format::BC6H_UF16 &&
            ktx2_wicked_format(KTX2UploadEncoding::Rgba16Float, false) ==
                wi::graphics::Format::R16G16B16A16_FLOAT,
            "HDR upload maps to Wicked's BC6H or RGBA16F formats");
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
    const KTX2UploadFormats supported = query_ktx2_upload_formats(wi::graphics::GetDevice(), false);
    const KTX2UploadEncoding expected = choose_ktx2_upload_encoding(
        false, supported, KTX2TextureUsage::NormalData);
    return check(resource.IsValid() && resource.GetTexture().IsValid() &&
        resource.GetTexture().GetDesc().format == ktx2_wicked_format(expected, false),
        "KTX2 normal data uses a two-channel BC5 or four-channel RGBA format");
}

inline bool check_ktx2_hdr_upload(const std::filesystem::path& path) {
    if (!check(std::filesystem::is_regular_file(path), "KTX2 HDR artifact present")) return false;
    const wi::Resource resource = load_ktx2_texture_resource(path.lexically_normal().string());
    const KTX2UploadFormats supported = query_ktx2_upload_formats(wi::graphics::GetDevice(), false);
    const KTX2UploadEncoding expected = choose_ktx2_upload_encoding(
        false, supported, KTX2TextureUsage::Color, true);
    if (!check(resource.IsValid() && resource.GetTexture().IsValid() &&
        (expected == KTX2UploadEncoding::Bc6h || expected == KTX2UploadEncoding::Rgba16Float) &&
        resource.GetTexture().GetDesc().format == ktx2_wicked_format(expected, false),
        "KTX2 HDR uploads to BC6H or a range-preserving RGBA16F fallback")) return false;

    if (!check(supported.rgba16f, "HDR fallback probe has RGBA16F device support")) return false;
    std::vector<uint8_t> encoded;
    if (!check(read_bounded_ktx2_container(path.lexically_normal().string(), encoded),
        "HDR fallback fixture reads within its container budget")) return false;
    KTX2UploadFormats fallback_formats = supported;
    fallback_formats.bc6h = false;
    const wi::Resource fallback = elisa::rendering::textures::detail::load_ktx2_texture_resource(
        encoded, KTX2TextureUsage::Color, &fallback_formats);
    if (!check(fallback.IsValid() && fallback.GetTexture().IsValid() &&
        fallback.GetTexture().GetDesc().format == wi::graphics::Format::R16G16B16A16_FLOAT,
        "HDR fallback transcodes and uploads as RGBA16F")) return false;

    wi::graphics::GetDevice()->WaitForGPU();
    wi::vector<uint8_t> readback;
    if (!check(wi::helper::saveTextureToMemory(fallback.GetTexture(), readback) &&
        readback.size() >= 4u * 4u * 8u, "HDR fallback texture reads back as half floats")) return false;
    bool retained_hdr_range = false;
    for (size_t pixel = 0; pixel < 16 && !retained_hdr_range; ++pixel) {
        for (size_t channel = 0; channel < 3; ++channel) {
            uint16_t encoded_channel = 0;
            std::memcpy(&encoded_channel, readback.data() + pixel * 8u + channel * 2u,
                sizeof(encoded_channel));
            const float value = wi::math::f16tof32(encoded_channel);
            retained_hdr_range = retained_hdr_range || (std::isfinite(value) && value > 1.0f);
        }
    }
    if (!check(retained_hdr_range,
        "HDR RGBA16F GPU readback retains values above linear white")) return false;
    std::fprintf(stdout,
        "KTX2 HDR upload: device-selected format and forced RGBA16F preserve values above 1.0\n");
    return true;
}

inline bool check_ktx2_upload_fixtures(const std::filesystem::path& cooked_directory) {
    return check_ktx2_bounded_reader(cooked_directory) && check_ktx2_format_policy() &&
        check_ktx2_color_upload(cooked_directory / "maze_tile_tex.ktx2") &&
        check_ktx2_normal_data_upload(cooked_directory / "maze_tile_normal.ktx2") &&
        check_ktx2_hdr_upload(cooked_directory / "maze_tile_hdr.ktx2") &&
        check_ktx2_cubemap_upload(cooked_directory / "maze_tile_cube.ktx2") &&
        check_ktx2_alpha_upload(cooked_directory / "maze_tile_alpha.ktx2");
}

inline int run_ktx2_upload_smoke(const std::filesystem::path& scene_manifest) {
    const std::filesystem::path project_root = scene_manifest.parent_path().parent_path();
    return check_ktx2_upload_fixtures(project_root / "build/cooked") ? 0 : 1;
}

} // namespace probe
