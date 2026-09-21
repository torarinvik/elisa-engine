#pragma once

// Encoded images stored as sections of a cooked ELPK bundle. Only PNG and
// JPEG, which Wicked decodes from memory, are accepted. The image header's
// dimensions are bounded before any decoder allocates pixels.
#include "virtual_package.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace elisa::assets {

inline constexpr uint32_t MAX_BUNDLE_TEXTURE_DIMENSION = 8192;
inline constexpr size_t MAX_BUNDLE_TEXTURE_BYTES = probe::BinaryPackageIndex::MAX_UNPACKED_BYTES;
inline constexpr size_t MAX_BUNDLE_SECTION_NAME_BYTES = 15;

enum class EncodedImageFormat { Png, Jpeg };

struct EncodedImageHeader {
    EncodedImageFormat format = EncodedImageFormat::Png;
    uint32_t width = 0;
    uint32_t height = 0;
};

struct BundleTexture {
    std::vector<uint8_t> encoded;
    EncodedImageHeader header;
    uint32_t checksum = 0;
};

// Section names match the cooker's rule: 1 to 15 bytes of [a-z0-9_].
inline bool valid_bundle_section_name(const char* name) {
    if (name == nullptr || name[0] == '\0') return false;
    for (size_t index = 0; index <= MAX_BUNDLE_SECTION_NAME_BYTES; ++index) {
        const char value = name[index];
        if (value == '\0') return true;
        if (index == MAX_BUNDLE_SECTION_NAME_BYTES) return false;
        const bool allowed = (value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') || value == '_';
        if (!allowed) return false;
    }
    return false;
}

inline uint32_t big_endian_u32(const std::vector<uint8_t>& bytes, size_t offset) {
    return (static_cast<uint32_t>(bytes[offset]) << 24) | (static_cast<uint32_t>(bytes[offset + 1]) << 16) |
        (static_cast<uint32_t>(bytes[offset + 2]) << 8) | static_cast<uint32_t>(bytes[offset + 3]);
}

inline uint16_t big_endian_u16(const std::vector<uint8_t>& bytes, size_t offset) {
    return static_cast<uint16_t>((bytes[offset] << 8) | bytes[offset + 1]);
}

// The PNG signature must be followed by a 13-byte IHDR chunk.
inline bool inspect_png(const std::vector<uint8_t>& bytes, EncodedImageHeader& header) {
    static constexpr uint8_t SIGNATURE[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    if (bytes.size() < 33 || !std::equal(SIGNATURE, SIGNATURE + 8, bytes.begin())) return false;
    if (big_endian_u32(bytes, 8) != 13 || bytes[12] != 'I' || bytes[13] != 'H' ||
        bytes[14] != 'D' || bytes[15] != 'R') return false;
    header.format = EncodedImageFormat::Png;
    header.width = big_endian_u32(bytes, 16);
    header.height = big_endian_u32(bytes, 20);
    return true;
}

// Walks JPEG marker segments up to the first frame header (SOFn). A scan or
// end-of-image marker before any frame header rejects the image.
inline bool inspect_jpeg(const std::vector<uint8_t>& bytes, EncodedImageHeader& header) {
    if (bytes.size() < 4 || bytes[0] != 0xFF || bytes[1] != 0xD8) return false;
    size_t offset = 2;
    while (offset + 4 <= bytes.size()) {
        if (bytes[offset] != 0xFF) return false;
        const uint8_t marker = bytes[offset + 1];
        if (marker == 0xFF) {
            ++offset;
            continue;
        }
        if (marker == 0xD8 || marker == 0xD9 || marker == 0xDA) return false;
        if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) {
            offset += 2;
            continue;
        }
        const size_t length = big_endian_u16(bytes, offset + 2);
        if (length < 2 || length > bytes.size() - offset - 2) return false;
        const bool frame = marker >= 0xC0 && marker <= 0xCF &&
            marker != 0xC4 && marker != 0xC8 && marker != 0xCC;
        if (frame) {
            if (length < 8) return false;
            header.format = EncodedImageFormat::Jpeg;
            header.height = big_endian_u16(bytes, offset + 5);
            header.width = big_endian_u16(bytes, offset + 7);
            return true;
        }
        offset += 2 + length;
    }
    return false;
}

inline bool inspect_encoded_image(const std::vector<uint8_t>& bytes, EncodedImageHeader& header,
        std::string& error) {
    header = EncodedImageHeader{};
    if (bytes.empty() || bytes.size() > MAX_BUNDLE_TEXTURE_BYTES) {
        error = "bundle texture size rejected";
        return false;
    }
    if (!inspect_png(bytes, header) && !inspect_jpeg(bytes, header)) {
        error = "bundle texture is not a PNG or JPEG image";
        return false;
    }
    if (header.width == 0 || header.height == 0 ||
        header.width > MAX_BUNDLE_TEXTURE_DIMENSION || header.height > MAX_BUNDLE_TEXTURE_DIMENSION) {
        error = "bundle texture dimensions rejected";
        return false;
    }
    return true;
}

inline const char* encoded_image_extension(EncodedImageFormat format) {
    return format == EncodedImageFormat::Png ? "png" : "jpg";
}

// Reads one CRC-checked section. The unpacked size is bounded before the
// section is read, and the image header before the bytes are returned.
inline bool read_bundle_texture(const std::filesystem::path& bundle_path, const char* section,
        BundleTexture& texture, std::string& error,
        probe::BinaryPackageReadCancellationCheck cancellation_check = {}) {
    texture = BundleTexture{};
    if (!valid_bundle_section_name(section)) {
        error = "bundle texture section name rejected";
        return false;
    }
    const std::string path = bundle_path.string();
    const probe::BinaryPackageIndex index = probe::read_binary_package_index(path);
    if (!index.valid) {
        error = index.error.empty() ? "bundle index rejected" : index.error;
        return false;
    }
    const std::string name(section);
    const auto entry = std::find_if(index.sections.begin(), index.sections.end(),
        [&name](const probe::BinaryPackageSection& candidate) { return candidate.name == name; });
    if (entry == index.sections.end()) {
        error = "bundle texture section is missing";
        return false;
    }
    if (entry->unpacked_size == 0 || entry->unpacked_size > MAX_BUNDLE_TEXTURE_BYTES) {
        error = "bundle texture size rejected";
        return false;
    }
    std::vector<uint8_t> bytes;
    if (!probe::read_binary_package_section(path, index, name, bytes, error,
            std::move(cancellation_check))) return false;
    if (!inspect_encoded_image(bytes, texture.header, error)) return false;
    texture.checksum = entry->checksum;
    texture.encoded = std::move(bytes);
    return true;
}

} // namespace elisa::assets
