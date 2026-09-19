#pragma once

// PNG capture kept outside Wicked's stb encoder. The raw texture readback is
// still supplied by Wicked; this writer uses stored DEFLATE blocks so graphics
// sanitizer runs do not enter the upstream PNG callback path.
#include "wiHelper.h"
#include "wiGraphics.h"

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace probe {

inline uint32_t png_crc(const uint8_t* data, size_t size) {
    uint32_t crc = 0xffffffffu;
    for (size_t index = 0; index < size; ++index) {
        crc ^= data[index];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

inline void png_u32(std::vector<uint8_t>& output, uint32_t value) {
    output.push_back(uint8_t(value >> 24));
    output.push_back(uint8_t(value >> 16));
    output.push_back(uint8_t(value >> 8));
    output.push_back(uint8_t(value));
}

inline void png_chunk(std::vector<uint8_t>& output, const char type[4], const std::vector<uint8_t>& data) {
    png_u32(output, uint32_t(data.size()));
    const size_t start = output.size();
    output.insert(output.end(), type, type + 4);
    output.insert(output.end(), data.begin(), data.end());
    png_u32(output, png_crc(output.data() + start, 4 + data.size()));
}

inline bool save_rgba_png(const wi::graphics::Texture& texture, const std::string& path) {
    wi::vector<uint8_t> raw;
    if (!wi::helper::saveTextureToMemoryFile(texture, "RAW", raw)) {
        return false;
    }
    const auto desc = texture.GetDesc();
    const size_t row_size = size_t(desc.width) * 4;
    if (desc.width == 0 || desc.height == 0 || raw.size() < row_size * desc.height) {
        return false;
    }

    std::vector<uint8_t> scanlines;
    scanlines.reserve((row_size + 1) * desc.height);
    for (uint32_t row = 0; row < desc.height; ++row) {
        scanlines.push_back(0);
        const size_t source_row = size_t(row) * row_size;
        scanlines.insert(scanlines.end(), raw.begin() + source_row, raw.begin() + source_row + row_size);
    }

    std::vector<uint8_t> zlib{0x78, 0x01};
    size_t offset = 0;
    while (offset < scanlines.size()) {
        const uint16_t length = uint16_t(std::min<size_t>(65535, scanlines.size() - offset));
        const bool final_block = offset + length == scanlines.size();
        zlib.push_back(final_block ? 1 : 0);
        zlib.push_back(uint8_t(length));
        zlib.push_back(uint8_t(length >> 8));
        const uint16_t inverse = uint16_t(~length);
        zlib.push_back(uint8_t(inverse));
        zlib.push_back(uint8_t(inverse >> 8));
        zlib.insert(zlib.end(), scanlines.begin() + offset, scanlines.begin() + offset + length);
        offset += length;
    }
    uint32_t adler = 1;
    uint32_t sum_a = 1;
    uint32_t sum_b = 0;
    for (uint8_t byte : scanlines) {
        sum_a = (sum_a + byte) % 65521u;
        sum_b = (sum_b + sum_a) % 65521u;
    }
    adler = (sum_b << 16) | sum_a;
    png_u32(zlib, adler);

    std::vector<uint8_t> png{0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    std::vector<uint8_t> header;
    png_u32(header, desc.width);
    png_u32(header, desc.height);
    header.insert(header.end(), {8, 6, 0, 0, 0});
    png_chunk(png, "IHDR", header);
    png_chunk(png, "IDAT", zlib);
    png_chunk(png, "IEND", {});
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    file.write(reinterpret_cast<const char*>(png.data()), std::streamsize(png.size()));
    return bool(file);
}

} // namespace probe
