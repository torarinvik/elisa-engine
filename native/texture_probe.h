#pragma once
// Native consumption of the cooked texture package. The Godot host builds a
// real ImageTexture from it; the native host loads and validates the same
// package here. Uploading it into a Wicked texture is a later step, so this is
// data-level consumption and is labelled as such.
#include "probe_core.h"
#include "package_load.h"

#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace probe {

inline uint32_t ktx_read_u32(const std::vector<uint8_t>& bytes, size_t offset) {
    return (uint32_t)bytes[offset] | ((uint32_t)bytes[offset + 1] << 8) |
           ((uint32_t)bytes[offset + 2] << 16) | ((uint32_t)bytes[offset + 3] << 24);
}

inline bool probe_texture_ktx(const std::string& ktx_path) {
    std::ifstream input(ktx_path, std::ios::binary);
    if (!check(input.good(), "KTX texture readable")) {
        return false;
    }
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    static const uint8_t identifier[12] = {0xAB, 0x4B, 0x54, 0x58, 0x20, 0x31, 0x31, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};
    if (!check(bytes.size() >= 68, "KTX file holds an identifier and header")) {
        return false;
    }
    if (!check(std::memcmp(bytes.data(), identifier, sizeof(identifier)) == 0, "KTX identifier matches")) {
        return false;
    }
    if (!check(ktx_read_u32(bytes, 12) == 0x04030201, "KTX is little-endian")) {
        return false;
    }
    const uint32_t gl_type = ktx_read_u32(bytes, 16);
    const uint32_t gl_format = ktx_read_u32(bytes, 24);
    const uint32_t gl_internal = ktx_read_u32(bytes, 28);
    const uint32_t width = ktx_read_u32(bytes, 36);
    const uint32_t height = ktx_read_u32(bytes, 40);
    const uint32_t faces = ktx_read_u32(bytes, 52);
    const uint32_t mips = ktx_read_u32(bytes, 56);
    if (!check(width == 4 && height == 4 && faces == 1 && mips == 1, "KTX is one 4x4 mip")) {
        return false;
    }
    const uint32_t image_bytes = ktx_read_u32(bytes, 64);
    if (!check(bytes.size() == 68 + (size_t)image_bytes, "KTX payload matches its declared image size")) {
        return false;
    }
    if (gl_internal == 0x83F0) {
        if (!check(gl_type == 0 && gl_format == 0 && image_bytes == 8, "KTX BC1 header agrees with the block")) {
            return false;
        }
        const uint32_t endpoint = (uint32_t)bytes[68] | ((uint32_t)bytes[69] << 8);
        const unsigned r = (endpoint >> 11) & 0x1F;
        const unsigned g = (endpoint >> 5) & 0x3F;
        const unsigned b = endpoint & 0x1F;
        if (!check(r == (26u >> 3) && g == (229u >> 2) && b == (51u >> 3), "KTX BC1 block is the cooked green")) {
            return false;
        }
        std::fprintf(stdout, "ktx texture: %ux%u internal=0x%04X image_bytes=%u endpoint=0x%04X\n",
            width, height, gl_internal, image_bytes, endpoint);
        return true;
    }
    if (!check(gl_internal == 0x8058 && gl_type == 0x1401 && gl_format == 0x1908 && image_bytes == 64,
            "KTX RGBA header agrees with the pixels")) {
        return false;
    }
    if (!check(bytes[68] == 26 && bytes[69] == 229 && bytes[70] == 51, "KTX RGBA pixels are the cooked green")) {
        return false;
    }
    std::fprintf(stdout, "ktx texture: %ux%u internal=0x%04X image_bytes=%u first=0x%02X\n",
        width, height, gl_internal, image_bytes, (unsigned)bytes[68]);
    return true;
}

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

inline bool probe_texture_packed(const std::string& package_path) {
    std::ifstream input(package_path);
    if (!check(input.good(), "packed texture package readable")) {
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
    if (!check(values["format"] == "elisa-texture-v1" && values["packing"] == "rgb565",
            "packed texture format and packing")) {
        return false;
    }
    const int width = std::stoi(values["width"]);
    const int height = std::stoi(values["height"]);
    const std::vector<uint8_t> pixels = decode_base64(values["pixels_b64"]);
    if (!check(width == 4 && height == 4 && pixels.size() == (size_t)(width * height * 2),
            "packed texture payload is two bytes per pixel")) {
        return false;
    }
    std::fprintf(stdout, "packed texture: %dx%d bytes_per_pixel=2 bytes=%u\n",
        width, height, (unsigned)pixels.size());
    return true;
}

inline bool probe_texture_bc1(const std::string& package_path) {
    std::ifstream input(package_path);
    if (!check(input.good(), "BC1 texture package readable")) {
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
    if (!check(values["format"] == "elisa-texture-v1" && values["packing"] == "bc1" && values["block_bytes"] == "8",
            "BC1 texture format, packing, and block size")) {
        return false;
    }
    const int width = std::stoi(values["width"]);
    const int height = std::stoi(values["height"]);
    const std::vector<uint8_t> pixels = decode_base64(values["pixels_b64"]);
    if (!check(width == 4 && height == 4 && pixels.size() == 8, "BC1 texture is one 8-byte block")) {
        return false;
    }
    std::fprintf(stdout, "bc1 texture: %dx%d block_bytes=8 bytes=%u\n",
        width, height, (unsigned)pixels.size());
    return true;
}

} // namespace probe
