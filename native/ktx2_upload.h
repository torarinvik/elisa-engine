#pragma once

// Bounded Basis/KTX2 decode at the native asset boundary. The transcoder owns
// compressed-container parsing; Wicked receives only a validated RGBA texture.
#include "probe_core.h"
#include "wiGraphicsDevice.h"
#include "wiResourceManager.h"

#include "basisu_transcoder.h"

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
    if (!check(width > 0 && height > 0 && width <= 4096 && height <= 4096 && transcoder.get_levels() > 0,
        "KTX2 upload dimensions")) return resource;
    if (!check(transcoder.start_transcoding(), "KTX2 upload starts transcoding")) return resource;
    const size_t pixel_bytes = static_cast<size_t>(width) * height * 4;
    std::vector<uint8_t> rgba(pixel_bytes);
    if (!check(transcoder.transcode_image_level(0, 0, 0, rgba.data(), width * height,
        basist::transcoder_texture_format::cTFRGBA32, 0, width, height),
        "KTX2 upload transcodes RGBA")) return resource;
    wi::graphics::TextureDesc desc;
    desc.width = width;
    desc.height = height;
    desc.depth = 1;
    desc.array_size = 1;
    desc.mip_levels = 1;
    desc.sample_count = 1;
    desc.format = wi::graphics::Format::R8G8B8A8_UNORM;
    desc.bind_flags = wi::graphics::BindFlag::SHADER_RESOURCE;
    wi::graphics::SubresourceData data;
    data.data_ptr = rgba.data();
    data.row_pitch = width * 4;
    data.slice_pitch = static_cast<uint32_t>(pixel_bytes);
    wi::graphics::Texture texture;
    if (!check(wi::graphics::GetDevice()->CreateTexture(&desc, &data, &texture), "KTX2 upload GPU texture")) return resource;
    resource.SetTexture(texture);
    return resource;
}

} // namespace probe
