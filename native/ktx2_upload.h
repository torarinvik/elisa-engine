#pragma once

// Bounded Basis/KTX2 transcode at the native asset boundary. Wicked receives
// queried GPU-native blocks where supported, with a validated RGBA fallback.
#include "ktx2_format_policy.h"
#include "wiGraphicsDevice.h"
#include "wiResourceManager.h"

#include "basisu_transcoder.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
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
            ktx2_format_supported(device, wi::graphics::Format::BC5_UNORM),
            ktx2_format_supported(device, wi::graphics::Format::BC6H_UF16),
            ktx2_format_supported(device, wi::graphics::Format::R16G16B16A16_FLOAT),
        };
        cache.srgb = {
            ktx2_format_supported(device, wi::graphics::Format::R8G8B8A8_UNORM_SRGB),
            ktx2_format_supported(device, wi::graphics::Format::BC1_UNORM_SRGB),
            ktx2_format_supported(device, wi::graphics::Format::BC3_UNORM_SRGB),
            ktx2_format_supported(device, wi::graphics::Format::BC7_UNORM_SRGB),
            ktx2_format_supported(device, wi::graphics::Format::BC5_UNORM),
            cache.linear.bc6h,
            cache.linear.rgba16f,
        };
    }
    return srgb ? cache.srgb : cache.linear;
}

inline basist::transcoder_texture_format ktx2_basis_format(KTX2UploadEncoding encoding) {
    switch (encoding) {
    case KTX2UploadEncoding::Bc1: return basist::transcoder_texture_format::cTFBC1_RGB;
    case KTX2UploadEncoding::Bc3: return basist::transcoder_texture_format::cTFBC3_RGBA;
    case KTX2UploadEncoding::Bc5: return basist::transcoder_texture_format::cTFBC5_RG;
    case KTX2UploadEncoding::Bc6h: return basist::transcoder_texture_format::cTFBC6H;
    case KTX2UploadEncoding::Bc7: return basist::transcoder_texture_format::cTFBC7_RGBA;
    case KTX2UploadEncoding::Rgba16Float: return basist::transcoder_texture_format::cTFRGBA_HALF;
    default: return basist::transcoder_texture_format::cTFRGBA32;
    }
}

inline wi::graphics::Format ktx2_wicked_format(KTX2UploadEncoding encoding, bool srgb) {
    switch (encoding) {
    case KTX2UploadEncoding::Bc1:
        return srgb ? wi::graphics::Format::BC1_UNORM_SRGB : wi::graphics::Format::BC1_UNORM;
    case KTX2UploadEncoding::Bc3:
        return srgb ? wi::graphics::Format::BC3_UNORM_SRGB : wi::graphics::Format::BC3_UNORM;
    case KTX2UploadEncoding::Bc5:
        return wi::graphics::Format::BC5_UNORM;
    case KTX2UploadEncoding::Bc6h:
        return wi::graphics::Format::BC6H_UF16;
    case KTX2UploadEncoding::Bc7:
        return srgb ? wi::graphics::Format::BC7_UNORM_SRGB : wi::graphics::Format::BC7_UNORM;
    case KTX2UploadEncoding::Rgba16Float:
        return wi::graphics::Format::R16G16B16A16_FLOAT;
    default:
        return srgb ? wi::graphics::Format::R8G8B8A8_UNORM_SRGB : wi::graphics::Format::R8G8B8A8_UNORM;
    }
}

inline uint32_t ktx2_block_bytes(KTX2UploadEncoding encoding) {
    return encoding == KTX2UploadEncoding::Bc1 ? 8u : 16u;
}

inline bool ktx2_encoding_is_compressed(KTX2UploadEncoding encoding) {
    return encoding == KTX2UploadEncoding::Bc1 || encoding == KTX2UploadEncoding::Bc3 ||
        encoding == KTX2UploadEncoding::Bc5 || encoding == KTX2UploadEncoding::Bc6h ||
        encoding == KTX2UploadEncoding::Bc7;
}

struct KTX2ChannelSwizzle {
    // r, g, b, a select a source channel; 0 and 1 select constants.
    std::array<uint8_t, 4> source{0, 1, 2, 3};

    bool identity() const { return source == std::array<uint8_t, 4>{0, 1, 2, 3}; }
};

inline bool ktx2_read_channel_swizzle(const basist::ktx2_transcoder& transcoder,
    KTX2ChannelSwizzle& swizzle) {
    const basisu::uint8_vec* value = transcoder.find_key("KTXswizzle");
    if (value == nullptr) return true;
    // Basis retains the KTX value (including its terminating NUL) and appends
    // one more NUL to every value returned by find_key().
    if (value->size() != 6 || (*value)[4] != 0 || (*value)[5] != 0) return false;
    for (size_t channel = 0; channel < swizzle.source.size(); ++channel) {
        switch ((*value)[channel]) {
        case 'r': swizzle.source[channel] = 0; break;
        case 'g': swizzle.source[channel] = 1; break;
        case 'b': swizzle.source[channel] = 2; break;
        case 'a': swizzle.source[channel] = 3; break;
        case '0': swizzle.source[channel] = 4; break;
        case '1': swizzle.source[channel] = 5; break;
        default: return false;
        }
    }
    return true;
}

inline void ktx2_apply_swizzle_rgba8(uint8_t* pixels, size_t pixel_count,
    const KTX2ChannelSwizzle& swizzle) {
    for (size_t pixel = 0; pixel < pixel_count; ++pixel) {
        uint8_t* channels = pixels + pixel * 4u;
        const std::array<uint8_t, 4> source{channels[0], channels[1], channels[2], channels[3]};
        for (size_t channel = 0; channel < source.size(); ++channel) {
            const uint8_t selected = swizzle.source[channel];
            channels[channel] = selected < 4 ? source[selected] : (selected == 4 ? 0 : 255);
        }
    }
}

inline uint8_t ktx2_linearize_srgb_channel(uint8_t encoded) {
    static const std::array<uint8_t, 256> lookup = [] {
        std::array<uint8_t, 256> values{};
        for (size_t value = 0; value < values.size(); ++value) {
            const double encoded_value = static_cast<double>(value) / 255.0;
            const double linear_value = encoded_value <= 0.04045 ? encoded_value / 12.92 :
                std::pow((encoded_value + 0.055) / 1.055, 2.4);
            values[value] = static_cast<uint8_t>(std::lround(linear_value * 255.0));
        }
        return values;
    }();
    return lookup[encoded];
}

inline void ktx2_linearize_srgb_rgba8(uint8_t* pixels, size_t pixel_count) {
    for (size_t pixel = 0; pixel < pixel_count; ++pixel) {
        uint8_t* channels = pixels + pixel * 4u;
        channels[0] = ktx2_linearize_srgb_channel(channels[0]);
        channels[1] = ktx2_linearize_srgb_channel(channels[1]);
        channels[2] = ktx2_linearize_srgb_channel(channels[2]);
    }
}

inline void ktx2_apply_swizzle_rgba16f(uint8_t* pixels, size_t pixel_count,
    const KTX2ChannelSwizzle& swizzle) {
    for (size_t pixel = 0; pixel < pixel_count; ++pixel) {
        uint8_t* pixel_bytes = pixels + pixel * 8u;
        std::array<uint16_t, 4> source{};
        std::memcpy(source.data(), pixel_bytes, sizeof(source));
        std::array<uint16_t, 4> channels{};
        for (size_t channel = 0; channel < source.size(); ++channel) {
            const uint8_t selected = swizzle.source[channel];
            channels[channel] = selected < 4 ? source[selected] : (selected == 4 ? 0x0000u : 0x3C00u);
        }
        std::memcpy(pixel_bytes, channels.data(), sizeof(channels));
    }
}

inline uint32_t ktx2_pixel_bytes(KTX2UploadEncoding encoding) {
    return encoding == KTX2UploadEncoding::Rgba16Float ? 8u : 4u;
}

namespace detail {

inline bool ktx2_formats_are_subset(const KTX2UploadFormats& requested,
    const KTX2UploadFormats& available) {
    return (!requested.rgba8 || available.rgba8) && (!requested.bc1 || available.bc1) &&
        (!requested.bc3 || available.bc3) && (!requested.bc7 || available.bc7) &&
        (!requested.bc5 || available.bc5) && (!requested.bc6h || available.bc6h) &&
        (!requested.rgba16f || available.rgba16f);
}

// The optional mask lets native probes exercise a real fallback path on a
// device that supports a higher-priority format. Callers may only remove
// formats the active device actually supports.
inline wi::Resource load_ktx2_texture_resource(const std::vector<uint8_t>& bytes,
    KTX2TextureUsage usage, const KTX2UploadFormats* format_mask) {
    wi::Resource resource;
    if (!ktx2_upload_check(!bytes.empty() && bytes.size() <= MAX_KTX2_CONTAINER_BYTES,
        "container is empty or exceeds its memory budget")) return resource;
    basist::basisu_transcoder_init();
    basist::ktx2_transcoder transcoder;
    if (!ktx2_upload_check(transcoder.init(bytes.data(), static_cast<uint32_t>(bytes.size())), "KTX2 upload parses")) return resource;
    KTX2ChannelSwizzle channel_swizzle;
    if (!ktx2_upload_check(ktx2_read_channel_swizzle(transcoder, channel_swizzle),
        "KTX2 swizzle metadata is a valid four-component mapping")) return resource;
    const uint32_t width = transcoder.get_width();
    const uint32_t height = transcoder.get_height();
    const uint32_t levels = transcoder.get_levels();
    if (!ktx2_upload_check(width > 0 && height > 0 && width <= 4096 && height <= 4096 && levels > 0 && levels <= 16,
        "KTX2 upload dimensions")) return resource;
    const uint32_t layers = transcoder.get_layers();
    const uint32_t faces = transcoder.get_faces();
    const bool is_cubemap = faces == 6;
    if (!ktx2_upload_check(ktx2_upload_shape_supported(layers, faces, width, height),
        "KTX2 upload is a 2D texture or cubemap")) return resource;
    wi::graphics::GraphicsDevice* device = wi::graphics::GetDevice();
    if (!ktx2_upload_check(device != nullptr, "KTX2 upload has a graphics device")) return resource;
    const bool hdr = transcoder.is_hdr();
    const bool srgb = !hdr && usage == KTX2TextureUsage::Color && transcoder.is_srgb();
    const bool linearize_swizzled_color = srgb && !channel_swizzle.identity();
    const KTX2UploadFormats available_formats = query_ktx2_upload_formats(device, srgb);
    if (format_mask != nullptr && !ktx2_formats_are_subset(*format_mask, available_formats)) {
        return resource;
    }
    const KTX2UploadFormats supported_formats =
        format_mask == nullptr ? available_formats : *format_mask;
    KTX2UploadEncoding encoding = choose_ktx2_upload_encoding(
        transcoder.get_has_alpha() != 0, supported_formats, usage, hdr);
    if (!channel_swizzle.identity()) {
        encoding = hdr ? (supported_formats.rgba16f ? KTX2UploadEncoding::Rgba16Float :
            KTX2UploadEncoding::Unsupported) : (supported_formats.rgba8 ? KTX2UploadEncoding::Rgba8 :
            KTX2UploadEncoding::Unsupported);
    }
    if (!ktx2_upload_check(encoding != KTX2UploadEncoding::Unsupported,
        "KTX2 upload has a supported native texture format")) return resource;
    if (!ktx2_upload_check(transcoder.start_transcoding(), "KTX2 upload starts transcoding")) return resource;
    constexpr size_t MAX_DECODED_BYTES = 64u * 1024u * 1024u;
    const size_t subresource_count = static_cast<size_t>(levels) * faces;
    std::vector<std::vector<uint8_t>> mip_bytes(subresource_count);
    std::vector<wi::graphics::SubresourceData> init_data(subresource_count);
    size_t decoded_bytes = 0;
    const bool is_compressed = ktx2_encoding_is_compressed(encoding);
    const uint32_t block_bytes = is_compressed ? ktx2_block_bytes(encoding) : 0;
    const uint32_t pixel_bytes = ktx2_pixel_bytes(encoding);
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
            const size_t mip_size = output_units * (is_compressed ? block_bytes : pixel_bytes);
            const size_t row_pitch = is_compressed ? blocks_x * block_bytes : mip_width * pixel_bytes;
            if (!ktx2_upload_check(output_units <= UINT32_MAX && mip_size <= MAX_DECODED_BYTES - decoded_bytes,
                "KTX2 upload decoded budget")) return resource;
            mip_bytes[subresource].resize(mip_size);
            if (!ktx2_upload_check(transcoder.transcode_image_level(level, 0, face, mip_bytes[subresource].data(),
                static_cast<uint32_t>(output_units), basis_format,
                0, static_cast<uint32_t>(is_compressed ? blocks_x : mip_width),
                is_compressed ? 0u : mip_height), "KTX2 upload transcodes selected mip format")) return resource;
            if (!channel_swizzle.identity()) {
                if (encoding == KTX2UploadEncoding::Rgba8) {
                    if (linearize_swizzled_color) {
                        ktx2_linearize_srgb_rgba8(mip_bytes[subresource].data(), pixel_count);
                    }
                    ktx2_apply_swizzle_rgba8(mip_bytes[subresource].data(), pixel_count, channel_swizzle);
                } else if (encoding == KTX2UploadEncoding::Rgba16Float) {
                    ktx2_apply_swizzle_rgba16f(mip_bytes[subresource].data(), pixel_count, channel_swizzle);
                } else {
                    return resource;
                }
            }
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
    desc.format = ktx2_wicked_format(encoding, srgb && !linearize_swizzled_color);
    desc.bind_flags = wi::graphics::BindFlag::SHADER_RESOURCE;
    if (is_cubemap) desc.misc_flags = wi::graphics::ResourceMiscFlag::TEXTURECUBE;
    wi::graphics::Texture texture;
    if (!ktx2_upload_check(device->CreateTexture(&desc, init_data.data(), &texture),
        "KTX2 upload GPU texture")) return resource;
    resource.SetTexture(texture);
    return resource;
}

} // namespace detail

inline wi::Resource load_ktx2_texture_resource(const std::vector<uint8_t>& bytes,
    KTX2TextureUsage usage = KTX2TextureUsage::Color) {
    return detail::load_ktx2_texture_resource(bytes, usage, nullptr);
}

inline wi::Resource load_ktx2_texture_resource(const std::string& texture_path,
    KTX2TextureUsage usage = KTX2TextureUsage::Color) {
    std::vector<uint8_t> bytes;
    if (!ktx2_upload_check(read_bounded_ktx2_container(texture_path, bytes),
        "cannot read a nonempty bounded complete container")) return {};
    return load_ktx2_texture_resource(bytes, usage);
}

} // namespace elisa::rendering::textures
