#pragma once

// CPU-only encoder for completed readback buffers. Row padding is not encoded.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace elisa::capture {

enum class PixelOrder { RGBA, BGRA, RGB10A2 };
// Bound scratch storage and the size of PNG's single IDAT chunk.
inline constexpr size_t MAX_RGBA_BYTES = 256u * 1024u * 1024u;

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

inline bool save_rgba_png(const uint8_t* raw, size_t size, uint32_t width,
                          uint32_t height, size_t row_pitch, PixelOrder order,
                          const std::string& path) {
    if (raw == nullptr || width == 0 || height == 0 || path.empty() ||
        (order != PixelOrder::RGBA && order != PixelOrder::BGRA && order != PixelOrder::RGB10A2)) return false;
    if (width > MAX_RGBA_BYTES / 4) return false;
    const size_t row_size = size_t(width) * 4;
    if (height > MAX_RGBA_BYTES / row_size || row_pitch < row_size) return false;
    // The last row needs pixels only, not trailing alignment padding.
    if (height > 1 && row_pitch > (std::numeric_limits<size_t>::max() - row_size) / (height - 1)) return false;
    const size_t required = size_t(height - 1) * row_pitch + row_size;
    if (size < required) return false;

    std::vector<uint8_t> scanlines;
    scanlines.reserve((row_size + 1) * height);
    for (uint32_t row = 0; row < height; ++row) {
        scanlines.push_back(0);
        const size_t source_row = size_t(row) * row_pitch;
        const size_t start = scanlines.size();
        scanlines.insert(scanlines.end(), raw + source_row, raw + source_row + row_size);
        if (order == PixelOrder::BGRA) {
            for (size_t pixel = start; pixel < start + row_size; pixel += 4) {
                std::swap(scanlines[pixel], scanlines[pixel + 2]);
            }
        } else if (order == PixelOrder::RGB10A2) {
            for (size_t pixel = 0; pixel < row_size; pixel += 4) {
                uint32_t packed = 0;
                std::memcpy(&packed, raw + source_row + pixel, sizeof(packed));
                scanlines[start + pixel] = uint8_t((packed & 1023u) * 255u / 1023u);
                scanlines[start + pixel + 1] = uint8_t(((packed >> 10) & 1023u) * 255u / 1023u);
                scanlines[start + pixel + 2] = uint8_t(((packed >> 20) & 1023u) * 255u / 1023u);
                scanlines[start + pixel + 3] = uint8_t(((packed >> 30) & 3u) * 85u);
            }
        }
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
    png_u32(header, width);
    png_u32(header, height);
    header.insert(header.end(), {8, 6, 0, 0, 0});
    png_chunk(png, "IHDR", header);
    png_chunk(png, "IDAT", zlib);
    png_chunk(png, "IEND", {});
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    file.write(reinterpret_cast<const char*>(png.data()), std::streamsize(png.size()));
    file.flush();
    if (!file) return false;
    file.close();
    return bool(file);
}

} // namespace elisa::capture
