#pragma once
// Native consumption of the cooked texture package. The Godot host builds a
// real ImageTexture from it; the native host loads and validates the same
// package here. Uploading it into a Wicked texture is a later step, so this is
// data-level consumption and is labelled as such.
#include "probe_core.h"
#include "package_load.h"

#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace probe {

inline bool probe_texture_package(const std::string& package_path) {
    std::ifstream input(package_path);
    if (!check(input.good(), "texture package readable")) {
        return false;
    }
    std::map<std::string, std::string> values;
    std::string line;
    while (std::getline(input, line)) {
        const auto separator = line.find('=');
        if (separator != std::string::npos) {
            values[line.substr(0, separator)] = line.substr(separator + 1);
        }
    }
    if (!check(values["format"] == "elisa-texture-v1", "texture package format")) {
        return false;
    }
    const int width = std::stoi(values["width"]);
    const int height = std::stoi(values["height"]);
    const std::vector<uint8_t> pixels = decode_base64(values["pixels_b64"]);
    if (!check(width == 4 && height == 4 && pixels.size() == (size_t)(width * height * 4),
            "texture pixel payload matches its dimensions")) {
        return false;
    }
    if (!check(pixels.front() == 26 && pixels[1] == 229 && pixels[2] == 51, "texture is the cooked green")) {
        return false;
    }
    std::fprintf(stdout, "texture: %dx%d channels=4 bytes=%u first=0x%02X\n",
        width, height, (unsigned)pixels.size(), (unsigned)pixels.front());
    return true;
}

} // namespace probe
