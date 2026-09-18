#pragma once
// Upload the cooked texture package into a real Wicked GPU texture and wrap it
// in a resource a material can sample. This is the native counterpart to the
// Godot host building an ImageTexture from the same package.
#include "probe_core.h"
#include "package_load.h"
#include "wiGraphicsDevice.h"
#include "wiResourceManager.h"

#include <fstream>
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

} // namespace probe
