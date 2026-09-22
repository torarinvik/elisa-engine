#pragma once

// Bounded Basis/KTX2 decode at the native asset boundary. The transcoder owns
// compressed-container parsing; Wicked receives validated RGBA 2D or cube data.
#include "probe_core.h"
#include "wiGraphicsDevice.h"
#include "wiResourceManager.h"

#include "basisu_transcoder.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace probe {

inline wi::Resource load_ktx2_texture_resource(const std::string& texture_path) {
    wi::Resource resource;
    std::ifstream input(texture_path, std::ios::binary);
    if (!check(input.good(), "KTX2 upload readable")) return resource;
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    constexpr size_t MAX_CONTAINER_BYTES = 64u * 1024u * 1024u;
    if (!check(!bytes.empty() && bytes.size() <= MAX_CONTAINER_BYTES, "KTX2 upload bounded container")) return resource;
    basist::basisu_transcoder_init();
    basist::ktx2_transcoder transcoder;
    if (!check(transcoder.init(bytes.data(), static_cast<uint32_t>(bytes.size())), "KTX2 upload parses")) return resource;
    const uint32_t width = transcoder.get_width();
    const uint32_t height = transcoder.get_height();
    const uint32_t levels = transcoder.get_levels();
    if (!check(width > 0 && height > 0 && width <= 4096 && height <= 4096 && levels > 0 && levels <= 16,
        "KTX2 upload dimensions")) return resource;
    const uint32_t layers = transcoder.get_layers();
    const uint32_t faces = transcoder.get_faces();
    const bool is_cubemap = faces == 6;
    if (!check(layers <= 1 && (faces == 1 || is_cubemap),
        "KTX2 upload is a 2D texture or cubemap")) return resource;
    if (!check(!is_cubemap || width == height, "KTX2 cubemap faces are square")) return resource;
    if (!check(transcoder.start_transcoding(), "KTX2 upload starts transcoding")) return resource;
    constexpr size_t MAX_DECODED_BYTES = 64u * 1024u * 1024u;
    std::vector<std::vector<uint8_t>> mip_bytes;
    std::vector<wi::graphics::SubresourceData> init_data;
    const size_t subresource_count = static_cast<size_t>(levels) * faces;
    mip_bytes.reserve(subresource_count);
    init_data.reserve(subresource_count);
    size_t decoded_bytes = 0;
    // Wicked orders subresources by array slice, then mip. Keep this ordering
    // so CreateTexture initializes each face of a cubemap correctly.
    for (uint32_t face = 0; face < faces; ++face) {
        for (uint32_t level = 0; level < levels; ++level) {
            basist::ktx2_image_level_info info{};
            if (!check(transcoder.get_image_level_info(info, level, 0, face), "KTX2 upload mip metadata")) return resource;
            const uint32_t mip_width = info.m_orig_width;
            const uint32_t mip_height = info.m_orig_height;
            const uint32_t expected_width = std::max(1u, width >> level);
            const uint32_t expected_height = std::max(1u, height >> level);
            if (!check(mip_width == expected_width && mip_height == expected_height,
                "KTX2 upload mip dimensions match texture")) return resource;
            const size_t pixel_count = static_cast<size_t>(mip_width) * mip_height;
            const size_t mip_size = pixel_count * 4;
            if (!check(pixel_count <= UINT32_MAX && mip_size <= MAX_DECODED_BYTES - decoded_bytes,
                "KTX2 upload decoded budget")) return resource;
            mip_bytes.emplace_back(mip_size);
            if (!check(transcoder.transcode_image_level(level, 0, face, mip_bytes.back().data(),
                static_cast<uint32_t>(pixel_count), basist::transcoder_texture_format::cTFRGBA32,
                0, mip_width, mip_height), "KTX2 upload transcodes RGBA mip")) return resource;
            wi::graphics::SubresourceData data;
            data.data_ptr = mip_bytes.back().data();
            data.row_pitch = mip_width * 4;
            data.slice_pitch = static_cast<uint32_t>(mip_size);
            init_data.push_back(data);
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
    desc.format = transcoder.is_srgb()
        ? wi::graphics::Format::R8G8B8A8_UNORM_SRGB
        : wi::graphics::Format::R8G8B8A8_UNORM;
    desc.bind_flags = wi::graphics::BindFlag::SHADER_RESOURCE;
    if (is_cubemap) desc.misc_flags = wi::graphics::ResourceMiscFlag::TEXTURECUBE;
    wi::graphics::Texture texture;
    if (!check(wi::graphics::GetDevice()->CreateTexture(&desc, init_data.data(), &texture),
        "KTX2 upload GPU texture")) return resource;
    resource.SetTexture(texture);
    return resource;
}

} // namespace probe
