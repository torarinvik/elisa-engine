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

// BGR10A2: packed 10-bit with blue in the low bits (Metal's BGR10A2Unorm,
// which Wicked's Metal backend uses for R10G10B10A2_UNORM).
enum class PixelOrder { RGBA, BGRA, RGB10A2, BGR10A2 };
// Bound accepted pixel storage and hence the encoded file size.
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
        (order != PixelOrder::RGBA && order != PixelOrder::BGRA && order != PixelOrder::RGB10A2 &&
         order != PixelOrder::BGR10A2)) return false;
    if (width > MAX_RGBA_BYTES / 4) return false;
    const size_t row_size = size_t(width) * 4;
    if (height > MAX_RGBA_BYTES / row_size || row_pitch < row_size) return false;
    // The last row needs pixels only, not trailing alignment padding.
    if (height > 1 && row_pitch > (std::numeric_limits<size_t>::max() - row_size) / (height - 1)) return false;
    const size_t required = size_t(height - 1) * row_pitch + row_size;
    if (size < required) return false;

    // Stored DEFLATE blocks are streamed directly into one IDAT chunk.
    // Pixel scratch uses one 64 KiB block, plus small metadata/stream buffers.
    const size_t filtered_size = (row_size + 1) * height;
    const size_t block_count = (filtered_size + 65534) / 65535;
    const size_t idat_size = filtered_size + block_count * 5 + 6;
    std::ofstream file(path, std::ios::binary);
    if (!file) return false;
    const auto write_bytes = [&](const uint8_t* bytes, size_t length) {
        file.write(reinterpret_cast<const char*>(bytes), std::streamsize(length));
    };
    const auto write_u32 = [&](uint32_t value) {
        const uint8_t bytes[] = {uint8_t(value >> 24), uint8_t(value >> 16),
            uint8_t(value >> 8), uint8_t(value)};
        write_bytes(bytes, sizeof(bytes));
    };
    std::vector<uint8_t> prefix{0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    std::vector<uint8_t> header;
    png_u32(header, width);
    png_u32(header, height);
    header.insert(header.end(), {8, 6, 0, 0, 0});
    png_chunk(prefix, "IHDR", header);
    write_bytes(prefix.data(), prefix.size());
    write_u32(uint32_t(idat_size));
    uint32_t crc = 0xffffffffu;
    const auto write_crc_bytes = [&](const uint8_t* bytes, size_t length) {
        write_bytes(bytes, length);
        for (size_t index = 0; index < length; ++index) {
            crc ^= bytes[index];
            for (int bit = 0; bit < 8; ++bit)
                crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
    };
    const uint8_t type[] = {'I', 'D', 'A', 'T'};
    write_crc_bytes(type, sizeof(type));
    const uint8_t zlib_header[] = {0x78, 0x01};
    write_crc_bytes(zlib_header, sizeof(zlib_header));
    std::vector<uint8_t> block;
    block.reserve(65535);
    size_t emitted = 0;
    uint32_t sum_a = 1;
    uint32_t sum_b = 0;
    const auto flush_block = [&]() {
        const uint16_t length = uint16_t(block.size());
        const uint16_t inverse = uint16_t(~length);
        const uint8_t block_header[] = {uint8_t(emitted + length == filtered_size ? 1 : 0),
            uint8_t(length), uint8_t(length >> 8), uint8_t(inverse), uint8_t(inverse >> 8)};
        write_crc_bytes(block_header, sizeof(block_header));
        write_crc_bytes(block.data(), block.size());
        emitted += block.size();
        block.clear();
    };
    const auto emit = [&](uint8_t value) {
        block.push_back(value);
        sum_a = (sum_a + value) % 65521u;
        sum_b = (sum_b + sum_a) % 65521u;
        if (block.size() == 65535) flush_block();
    };
    for (uint32_t row = 0; row < height; ++row) {
        emit(0); // PNG filter None
        const uint8_t* source = raw + size_t(row) * row_pitch;
        for (size_t pixel = 0; pixel < row_size; pixel += 4) {
            uint8_t rgba[4];
            std::memcpy(rgba, source + pixel, 4);
            if (order == PixelOrder::BGRA) {
                std::swap(rgba[0], rgba[2]);
            } else if (order == PixelOrder::RGB10A2 || order == PixelOrder::BGR10A2) {
                uint32_t packed = 0;
                std::memcpy(&packed, source + pixel, sizeof(packed));
                const size_t red = order == PixelOrder::RGB10A2 ? 0 : 2;
                rgba[red] = uint8_t((packed & 1023u) * 255u / 1023u);
                rgba[1] = uint8_t(((packed >> 10) & 1023u) * 255u / 1023u);
                rgba[2 - red] = uint8_t(((packed >> 20) & 1023u) * 255u / 1023u);
                rgba[3] = uint8_t(((packed >> 30) & 3u) * 85u);
            }
            for (uint8_t channel : rgba) emit(channel);
        }
    }
    if (!block.empty()) flush_block();
    const uint32_t adler = (sum_b << 16) | sum_a;
    const uint8_t checksum[] = {uint8_t(adler >> 24), uint8_t(adler >> 16),
        uint8_t(adler >> 8), uint8_t(adler)};
    write_crc_bytes(checksum, sizeof(checksum));
    write_u32(~crc);
    std::vector<uint8_t> ending;
    png_chunk(ending, "IEND", {});
    write_bytes(ending.data(), ending.size());
    file.flush();
    if (!file) return false;
    file.close();
    return bool(file);
}

} // namespace elisa::capture
