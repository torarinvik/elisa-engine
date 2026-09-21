#pragma once

// Bounded field decoders for text-indexed cooked packages: decimal counts,
// finite floats, and base64 streams of little-endian words and names.
#include "virtual_package.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace elisa::assets {

namespace detail {

inline bool decode_base64(const std::string& text, std::vector<uint8_t>& bytes) {
    if (text.empty() || text.size() % 4 != 0 || text.size() > 64u * 1024u * 1024u) return false;
    auto value = [](char character) -> int {
        if (character >= 'A' && character <= 'Z') return character - 'A';
        if (character >= 'a' && character <= 'z') return character - 'a' + 26;
        if (character >= '0' && character <= '9') return character - '0' + 52;
        if (character == '+') return 62;
        if (character == '/') return 63;
        return -1;
    };
    bytes.clear();
    bytes.reserve(text.size() / 4 * 3);
    for (size_t offset = 0; offset < text.size(); offset += 4) {
        const bool last = offset + 4 == text.size();
        const int a = value(text[offset]);
        const int b = value(text[offset + 1]);
        const bool pad_c = text[offset + 2] == '=';
        const bool pad_d = text[offset + 3] == '=';
        const int c = pad_c ? 0 : value(text[offset + 2]);
        const int d = pad_d ? 0 : value(text[offset + 3]);
        if (a < 0 || b < 0 || c < 0 || d < 0 || (pad_c && !pad_d) ||
            ((pad_c || pad_d) && !last) || (pad_c && (b & 15) != 0) ||
            (pad_d && !pad_c && (c & 3) != 0)) return false;
        const uint32_t word = (uint32_t(a) << 18) | (uint32_t(b) << 12) |
            (uint32_t(c) << 6) | uint32_t(d);
        bytes.push_back(uint8_t(word >> 16));
        if (!pad_c) bytes.push_back(uint8_t(word >> 8));
        if (!pad_d) bytes.push_back(uint8_t(word));
    }
    return true;
}

inline bool parse_count(const probe::PackageIndex& package, const char* key, uint64_t& value) {
    const auto found = package.sections.find(key);
    if (found == package.sections.end() || found->second.empty()) return false;
    value = 0;
    for (char character : found->second) {
        if (character < '0' || character > '9') return false;
        const uint64_t digit = uint64_t(character - '0');
        if (value > (std::numeric_limits<uint64_t>::max() - digit) / 10) return false;
        value = value * 10 + digit;
    }
    return true;
}

inline bool decode_floats(const probe::PackageIndex& package, const char* key,
    size_t count, std::vector<float>& output) {
    const auto found = package.sections.find(key);
    if (found == package.sections.end() || count > 16u * 1024u * 1024u / sizeof(float)) return false;
    std::vector<uint8_t> bytes;
    if (!decode_base64(found->second, bytes) || bytes.size() != count * sizeof(float)) return false;
    output.resize(count);
    for (size_t index = 0; index < count; ++index) {
        const uint32_t bits = uint32_t(bytes[index * 4]) |
            (uint32_t(bytes[index * 4 + 1]) << 8) |
            (uint32_t(bytes[index * 4 + 2]) << 16) |
            (uint32_t(bytes[index * 4 + 3]) << 24);
        std::memcpy(&output[index], &bits, sizeof(bits));
        if (!std::isfinite(output[index])) return false;
    }
    return true;
}

inline bool decode_u32(const probe::PackageIndex& package, const char* key,
    size_t count, std::vector<uint32_t>& output) {
    const auto found = package.sections.find(key);
    if (found == package.sections.end() || count > 16u * 1024u * 1024u / sizeof(uint32_t)) return false;
    std::vector<uint8_t> bytes;
    if (!decode_base64(found->second, bytes) || bytes.size() != count * sizeof(uint32_t)) return false;
    output.resize(count);
    for (size_t index = 0; index < count; ++index) {
        const size_t offset = index * 4;
        output[index] = uint32_t(bytes[offset]) | (uint32_t(bytes[offset + 1]) << 8) |
            (uint32_t(bytes[offset + 2]) << 16) | (uint32_t(bytes[offset + 3]) << 24);
    }
    return true;
}

inline bool decode_i32(const probe::PackageIndex& package, const char* key,
    size_t count, std::vector<int32_t>& output) {
    std::vector<uint32_t> raw;
    if (!decode_u32(package, key, count, raw)) return false;
    output.resize(count);
    for (size_t index = 0; index < count; ++index) {
        std::memcpy(&output[index], &raw[index], sizeof(int32_t));
    }
    return true;
}

inline bool decode_names(const std::vector<uint8_t>& bytes, size_t count,
    std::vector<std::string>& names) {
    size_t offset = 0;
    names.clear();
    names.reserve(count);
    for (size_t index = 0; index < count; ++index) {
        if (bytes.size() - offset < 4) return false;
        const uint32_t length = uint32_t(bytes[offset]) |
            (uint32_t(bytes[offset + 1]) << 8) |
            (uint32_t(bytes[offset + 2]) << 16) |
            (uint32_t(bytes[offset + 3]) << 24);
        offset += 4;
        if (length > bytes.size() - offset) return false;
        names.emplace_back(reinterpret_cast<const char*>(bytes.data() + offset), length);
        offset += length;
    }
    return offset == bytes.size();
}

inline bool parse_finite_float(const probe::PackageIndex& package, const std::string& key,
    float& value) {
    const auto found = package.sections.find(key);
    if (found == package.sections.end() || found->second.empty()) return false;
    char* end = nullptr;
    const float parsed = std::strtof(found->second.c_str(), &end);
    if (end != found->second.c_str() + found->second.size() || !std::isfinite(parsed)) return false;
    value = parsed;
    return true;
}

} // namespace detail

} // namespace elisa::assets
