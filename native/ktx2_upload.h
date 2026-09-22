#pragma once

// Bounded Basis/KTX2 transcode at the native asset boundary. Wicked receives
// queried GPU-native blocks where supported, with a validated RGBA fallback.
#include "ktx2_format_policy.h"
#include "wiGraphicsDevice.h"
#include "wiResourceManager.h"

#include "basisu_transcoder.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace elisa::rendering::textures {

inline bool ktx2_upload_check(bool value, const char* message) {
    if (!value) std::fprintf(stderr, "KTX2 texture upload failed: %s\n", message);
    return value;
}

constexpr uint64_t MAX_KTX2_CONTAINER_BYTES = 64ull * 1024ull * 1024ull;

inline bool read_bounded_ktx2_container(const std::string& texture_path, std::vector<uint8_t>& bytes) {
    bytes.clear();
    std::ifstream input(texture_path, std::ios::binary | std::ios::ate);
    if (!input.good()) return false;
    const std::streamoff file_size = input.tellg();
    if (file_size <= 0 || static_cast<uint64_t>(file_size) > MAX_KTX2_CONTAINER_BYTES) return false;
    input.seekg(0, std::ios::beg);
    bytes.resize(static_cast<size_t>(file_size));
    if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())).good() ||
        input.gcount() != static_cast<std::streamsize>(bytes.size())) {
        bytes.clear();
        return false;
    }
    return true;
}

struct KTX2FormatSupportCache {
    wi::graphics::GraphicsDevice* device = nullptr;
    KTX2UploadFormats linear;
    KTX2UploadFormats srgb;
};

inline bool ktx2_format_supported(wi::graphics::GraphicsDevice* device, wi::graphics::Format format) {
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
}

inline KTX2UploadFormats query_ktx2_upload_formats(wi::graphics::GraphicsDevice* device, bool srgb) {
    static std::mutex cache_mutex;
    static KTX2FormatSupportCache cache;
    std::lock_guard<std::mutex> lock(cache_mutex);
    if (cache.device != device) {
        cache.device = device;
        cache.linear = {
            ktx2_format_supported(device, wi::graphics::Format::R8G8B8A8_UNORM),
            ktx2_format_supported(device, wi::graphics::Format::BC1_UNORM),
            ktx2_format_supported(device, wi::graphics::Format::BC3_UNORM),
            ktx2_format_supported(device, wi::graphics::Format::BC7_UNORM),
        };
        cache.srgb = {
            ktx2_format_supported(device, wi::graphics::Format::R8G8B8A8_UNORM_SRGB),
            ktx2_format_supported(device, wi::graphics::Format::BC1_UNORM_SRGB),
            ktx2_format_supported(device, wi::graphics::Format::BC3_UNORM_SRGB),
            ktx2_format_supported(device, wi::graphics::Format::BC7_UNORM_SRGB),
        };
    }
    return srgb ? cache.srgb : cache.linear;
}

inline basist::transcoder_texture_format ktx2_basis_format(KTX2UploadEncoding encoding) {
    switch (encoding) {
    case KTX2UploadEncoding::Bc1: return basist::transcoder_texture_format::cTFBC1_RGB;
    case KTX2UploadEncoding::Bc3: return basist::transcoder_texture_format::cTFBC3_RGBA;
    case KTX2UploadEncoding::Bc7: return basist::transcoder_texture_format::cTFBC7_RGBA;
    default: return basist::transcoder_texture_format::cTFRGBA32;
    }
}

inline wi::graphics::Format ktx2_wicked_format(KTX2UploadEncoding encoding, bool srgb) {
    switch (encoding) {
    case KTX2UploadEncoding::Bc1:
        return srgb ? wi::graphics::Format::BC1_UNORM_SRGB : wi::graphics::Format::BC1_UNORM;
    case KTX2UploadEncoding::Bc3:
        return srgb ? wi::graphics::Format::BC3_UNORM_SRGB : wi::graphics::Format::BC3_UNORM;
    case KTX2UploadEncoding::Bc7:
        return srgb ? wi::graphics::Format::BC7_UNORM_SRGB : wi::graphics::Format::BC7_UNORM;
    default:
        return srgb ? wi::graphics::Format::R8G8B8A8_UNORM_SRGB : wi::graphics::Format::R8G8B8A8_UNORM;
    }
}

inline uint32_t ktx2_block_bytes(KTX2UploadEncoding encoding) {
    return encoding == KTX2UploadEncoding::Bc1 ? 8u : 16u;
}

inline wi::Resource load_ktx2_texture_resource(const std::vector<uint8_t>& bytes,
    KTX2TextureUsage usage = KTX2TextureUsage::Color) {
    wi::Resource resource;
    if (!ktx2_upload_check(!bytes.empty() && bytes.size() <= MAX_KTX2_CONTAINER_BYTES,
        "container is empty or exceeds its memory budget")) return resource;
    basist::basisu_transcoder_init();
    basist::ktx2_transcoder transcoder;
    if (!ktx2_upload_check(transcoder.init(bytes.data(), static_cast<uint32_t>(bytes.size())), "KTX2 upload parses")) return resource;
    const uint32_t width = transcoder.get_width();
    const uint32_t height = transcoder.get_height();
    const uint32_t levels = transcoder.get_levels();
    if (!ktx2_upload_check(width > 0 && height > 0 && width <= 4096 && height <= 4096 && levels > 0 && levels <= 16,
        "KTX2 upload dimensions")) return resource;
    const uint32_t layers = transcoder.get_layers();
    const uint32_t faces = transcoder.get_faces();
    const bool is_cubemap = faces == 6;
    if (!ktx2_upload_check(layers <= 1 && (faces == 1 || is_cubemap),
        "KTX2 upload is a 2D texture or cubemap")) return resource;
    if (!ktx2_upload_check(!is_cubemap || width == height, "KTX2 cubemap faces are square")) return resource;
    wi::graphics::GraphicsDevice* device = wi::graphics::GetDevice();
    if (!ktx2_upload_check(device != nullptr, "KTX2 upload has a graphics device")) return resource;
    const bool srgb = usage == KTX2TextureUsage::Color && transcoder.is_srgb();
    const KTX2UploadFormats supported_formats = query_ktx2_upload_formats(device, srgb);
    const KTX2UploadEncoding encoding = choose_ktx2_upload_encoding(
        transcoder.get_has_alpha() != 0, supported_formats, usage);
    if (!ktx2_upload_check(encoding != KTX2UploadEncoding::Unsupported,
        "KTX2 upload has a supported native texture format")) return resource;
    if (!ktx2_upload_check(transcoder.start_transcoding(), "KTX2 upload starts transcoding")) return resource;
    constexpr size_t MAX_DECODED_BYTES = 64u * 1024u * 1024u;
    const size_t subresource_count = static_cast<size_t>(levels) * faces;
    std::vector<std::vector<uint8_t>> mip_bytes(subresource_count);
    std::vector<wi::graphics::SubresourceData> init_data(subresource_count);
    size_t decoded_bytes = 0;
    const bool is_compressed = encoding != KTX2UploadEncoding::Rgba8;
    const uint32_t block_bytes = is_compressed ? ktx2_block_bytes(encoding) : 0;
    const basist::transcoder_texture_format basis_format = ktx2_basis_format(encoding);
    // Basis recommends finishing all faces at one mip before changing levels
    // (especially for Zstd). Store each result in Wicked's slice-major order.
    for (uint32_t level = 0; level < levels; ++level) {
        for (uint32_t face = 0; face < faces; ++face) {
            const size_t subresource = static_cast<size_t>(face) * levels + level;
            basist::ktx2_image_level_info info{};
            if (!ktx2_upload_check(transcoder.get_image_level_info(info, level, 0, face), "KTX2 upload mip metadata")) return resource;
            const uint32_t mip_width = info.m_orig_width;
            const uint32_t mip_height = info.m_orig_height;
            const uint32_t expected_width = std::max(1u, width >> level);
            const uint32_t expected_height = std::max(1u, height >> level);
            if (!ktx2_upload_check(mip_width == expected_width && mip_height == expected_height,
                "KTX2 upload mip dimensions match texture")) return resource;
            const size_t pixel_count = static_cast<size_t>(mip_width) * mip_height;
            const size_t blocks_x = (mip_width + 3u) / 4u;
            const size_t blocks_y = (mip_height + 3u) / 4u;
            const size_t output_units = is_compressed ? blocks_x * blocks_y : pixel_count;
            const size_t mip_size = output_units * (is_compressed ? block_bytes : 4u);
            const size_t row_pitch = is_compressed ? blocks_x * block_bytes : mip_width * 4u;
            if (!ktx2_upload_check(output_units <= UINT32_MAX && mip_size <= MAX_DECODED_BYTES - decoded_bytes,
                "KTX2 upload decoded budget")) return resource;
            mip_bytes[subresource].resize(mip_size);
            if (!ktx2_upload_check(transcoder.transcode_image_level(level, 0, face, mip_bytes[subresource].data(),
                static_cast<uint32_t>(output_units), basis_format,
                0, static_cast<uint32_t>(is_compressed ? blocks_x : mip_width),
                is_compressed ? 0u : mip_height), "KTX2 upload transcodes selected mip format")) return resource;
            wi::graphics::SubresourceData data;
            data.data_ptr = mip_bytes[subresource].data();
            data.row_pitch = static_cast<uint32_t>(row_pitch);
            data.slice_pitch = static_cast<uint32_t>(mip_size);
            init_data[subresource] = data;
            decoded_bytes += mip_size;
        }
    }
    wi::graphics::TextureDesc desc;
    desc.width = width;
    desc.height = height;
    desc.depth = 1;
    desc.array_size = faces;
    desc.mip_levels = levels;
    desc.sample_count = 1;
    desc.format = ktx2_wicked_format(encoding, srgb);
    desc.bind_flags = wi::graphics::BindFlag::SHADER_RESOURCE;
    if (is_cubemap) desc.misc_flags = wi::graphics::ResourceMiscFlag::TEXTURECUBE;
    wi::graphics::Texture texture;
    if (!ktx2_upload_check(device->CreateTexture(&desc, init_data.data(), &texture),
        "KTX2 upload GPU texture")) return resource;
    resource.SetTexture(texture);
    return resource;
}

inline wi::Resource load_ktx2_texture_resource(const std::string& texture_path,
    KTX2TextureUsage usage = KTX2TextureUsage::Color) {
    std::vector<uint8_t> bytes;
    if (!ktx2_upload_check(read_bounded_ktx2_container(texture_path, bytes),
        "cannot read a nonempty bounded complete container")) return {};
    return load_ktx2_texture_resource(bytes, usage);
}

} // namespace elisa::rendering::textures
