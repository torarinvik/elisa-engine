#pragma once
// Upload the cooked texture package into a real Wicked GPU texture and wrap it
// in a resource a material can sample. This is the native counterpart to the
// Godot host building an ImageTexture from the same package.
#include "probe_core.h"
#include "package_load.h"
#include "texture_probe.h"
#include "wiGraphicsDevice.h"
#include "wiResourceManager.h"

#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace probe {

inline wi::Resource load_texture_resource(const std::string& package_path) {
    wi::Resource resource;
    std::ifstream input(package_path);
    if (!check(input.good(), "texture upload package readable")) {
        return resource;
    }
    std::map<std::string, std::string> values;
    std::string line;
    while (std::getline(input, line)) {
        const auto separator = line.find('=');
        if (separator != std::string::npos) {
            values[line.substr(0, separator)] = line.substr(separator + 1);
        }
    }
    const int width = std::stoi(values["width"]);
    const int height = std::stoi(values["height"]);
    const std::vector<uint8_t> pixels = decode_base64(values["pixels_b64"]);
    if (!check(width == 4 && height == 4 && pixels.size() == (size_t)(width * height * 4),
            "texture upload payload matches dimensions")) {
        return resource;
    }
    wi::graphics::TextureDesc desc;
    desc.width = (uint32_t)width;
    desc.height = (uint32_t)height;
    desc.depth = 1;
    desc.array_size = 1;
    desc.mip_levels = 1;
    desc.sample_count = 1;
    desc.format = wi::graphics::Format::R8G8B8A8_UNORM;
    desc.bind_flags = wi::graphics::BindFlag::SHADER_RESOURCE;
    wi::graphics::SubresourceData data;
    data.data_ptr = pixels.data();
    data.row_pitch = (uint32_t)(width * 4);
    data.slice_pitch = (uint32_t)(width * height * 4);
    wi::graphics::Texture texture;
    if (!check(wi::graphics::GetDevice()->CreateTexture(&desc, &data, &texture), "texture upload")) {
        return resource;
    }
    resource.SetTexture(texture);
    return resource;
}

inline wi::Resource load_ktx1_texture_resource(const std::string& texture_path) {
    wi::Resource resource;
    std::ifstream input(texture_path, std::ios::binary);
    if (!check(input.good(), "KTX1 upload readable")) return resource;
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    static const uint8_t identifier[12] = {0xAB, 0x4B, 0x54, 0x58, 0x20, 0x31, 0x31, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};
    if (!check(bytes.size() >= 68 && std::memcmp(bytes.data(), identifier, sizeof(identifier)) == 0,
        "KTX1 upload identifier") ||
        !check(ktx_read_u32(bytes, 12) == 0x04030201 && ktx_read_u32(bytes, 16) == 0x1401 &&
            ktx_read_u32(bytes, 24) == 0x1908 && ktx_read_u32(bytes, 28) == 0x8058,
            "KTX1 upload RGBA format") ||
        !check(ktx_read_u32(bytes, 52) == 1 && ktx_read_u32(bytes, 56) == 1,
            "KTX1 upload single face and mip")) return resource;
    const uint32_t width = ktx_read_u32(bytes, 36);
    const uint32_t height = ktx_read_u32(bytes, 40);
    const uint32_t image_bytes = ktx_read_u32(bytes, 64);
    if (!check(width > 0 && height > 0 && width <= 4096 && height <= 4096 &&
        image_bytes == width * height * 4 && bytes.size() == 68u + image_bytes,
        "KTX1 upload bounded payload")) return resource;
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
    data.data_ptr = bytes.data() + 68;
    data.row_pitch = width * 4;
    data.slice_pitch = image_bytes;
    wi::graphics::Texture texture;
    if (!check(wi::graphics::GetDevice()->CreateTexture(&desc, &data, &texture), "KTX1 upload")) return resource;
    resource.SetTexture(texture);
    return resource;
}

} // namespace probe
