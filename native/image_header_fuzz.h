#pragma once

// Seeded mutation fuzz for the encoded-image boundary of bundle textures.
// Every accepted mutant must carry the format its signature names and
// dimensions inside the bundle limits; every refusal must name a reason.
#include "bundle_texture.h"
#include "cooked_geometry_fuzz.h"

#include <cstdint>
#include <string>
#include <vector>

namespace elisa::assets::fuzz {

struct ImageFuzzResult {
    size_t accepted = 0;
    size_t refused = 0;
    size_t broken = 0;
    uint64_t digest = 1469598103934665603ULL;
};

inline void put_be32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
    for (int shift = 0; shift < 4; ++shift) bytes[offset + shift] = static_cast<uint8_t>(value >> (24 - 8 * shift));
}

inline void put_le32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
    for (int shift = 0; shift < 4; ++shift) bytes[offset + shift] = static_cast<uint8_t>(value >> (8 * shift));
}

inline std::vector<uint8_t> png_seed() {
    std::vector<uint8_t> bytes = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    bytes.resize(45, 0);
    put_be32(bytes, 8, 13);
    bytes[12] = 'I'; bytes[13] = 'H'; bytes[14] = 'D'; bytes[15] = 'R';
    put_be32(bytes, 16, 64);
    put_be32(bytes, 20, 32);
    return bytes;
}

inline std::vector<uint8_t> jpeg_seed() {
    // SOI, an APP0 segment, then a baseline frame header 48 high, 96 wide.
    return {0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x04, 0x00, 0x00,
        0xFF, 0xC0, 0x00, 0x0B, 0x08, 0x00, 0x30, 0x00, 0x60, 0x01, 0x01, 0x11, 0x00,
        0xFF, 0xD9};
}

inline std::vector<uint8_t> ktx2_seed() {
    std::vector<uint8_t> bytes = {0xAB, 'K', 'T', 'X', ' ', '2', '0', 0xBB, '\r', '\n', 0x1A, '\n'};
    bytes.resize(KTX2_HEADER_BYTES + 16, 0);
    put_le32(bytes, 20, 256);
    put_le32(bytes, 24, 128);
    put_le32(bytes, 36, 1);
    put_le32(bytes, 40, 9);
    return bytes;
}

inline EncodedImageFormat signature_format(const std::vector<uint8_t>& bytes, bool& known) {
    EncodedImageHeader probe_header;
    known = true;
    if (inspect_png(bytes, probe_header)) return EncodedImageFormat::Png;
    if (inspect_jpeg(bytes, probe_header)) return EncodedImageFormat::Jpeg;
    if (inspect_ktx2(bytes, probe_header)) return EncodedImageFormat::Ktx2;
    known = false;
    return EncodedImageFormat::Png;
}

inline void mutate_image(std::vector<uint8_t>& bytes, uint32_t& seed) {
    const uint64_t kind = next_seed(seed) % 4;
    if (bytes.empty()) return;
    const size_t at = next_seed(seed) % bytes.size();
    if (kind == 0) {
        bytes[at] ^= static_cast<uint8_t>(1u << (next_seed(seed) % 8));
    } else if (kind == 1) {
        bytes[at] = static_cast<uint8_t>(next_seed(seed));
    } else if (kind == 2) {
        bytes.resize(at);
    } else {
        static constexpr uint32_t EXTREMES[] = {0, 1, 4096, 4097, 8192, 8193, 0x7FFFFFFF, 0xFFFFFFFF};
        if (at + 4 <= bytes.size()) put_be32(bytes, at, EXTREMES[next_seed(seed) % 8]);
    }
}

inline ImageFuzzResult fuzz_image_headers(uint32_t seed, size_t rounds) {
    ImageFuzzResult result;
    const std::vector<std::vector<uint8_t>> seeds = {png_seed(), jpeg_seed(), ktx2_seed()};
    for (size_t round = 0; round < rounds; ++round) {
        std::vector<uint8_t> bytes = seeds[round % seeds.size()];
        const size_t edits = 1 + next_seed(seed) % 4;
        for (size_t edit = 0; edit < edits; ++edit) mutate_image(bytes, seed);
        EncodedImageHeader header;
        std::string error;
        const bool ok = inspect_encoded_image(bytes, header, error);
        bool known = false;
        const EncodedImageFormat expected = signature_format(bytes, known);
        bool holds;
        if (ok) {
            const uint32_t limit = header.format == EncodedImageFormat::Ktx2 ?
                MAX_BUNDLE_KTX2_DIMENSION : MAX_BUNDLE_TEXTURE_DIMENSION;
            holds = known && header.format == expected && header.width > 0 && header.height > 0 &&
                header.width <= limit && header.height <= limit;
            ++result.accepted;
        } else {
            holds = !error.empty();
            ++result.refused;
        }
        if (!holds) ++result.broken;
        const uint64_t mixed = (ok ? 1ULL : 2ULL) ^ (static_cast<uint64_t>(header.width) << 8) ^
            (static_cast<uint64_t>(header.height) << 36);
        result.digest = (result.digest ^ mixed) * 1099511628211ULL;
    }
    return result;
}

} // namespace elisa::assets::fuzz
