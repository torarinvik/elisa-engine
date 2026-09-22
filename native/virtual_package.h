#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <fstream>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <system_error>
#include <vector>
#include <zstd.h>

namespace probe {

inline constexpr size_t BINARY_PACKAGE_READ_CHUNK_BYTES = size_t(64) * 1024;
// Called between bounded section-read and decompression work chunks. The
// counters describe bytes read from the encoded section.
using BinaryPackageReadCancellationCheck = std::function<bool(size_t, size_t)>;

struct PackageIndex {
    static constexpr size_t MAX_PACKAGE_BYTES = 64 * 1024 * 1024;
    static constexpr size_t MAX_LINE_BYTES = 16 * 1024 * 1024;
    static constexpr size_t MAX_SECTIONS = 128;

    std::map<std::string, std::string> sections;
    bool valid = false;
    std::string error;
};

inline bool safe_package_path(const std::string& value) {
    if (value.empty() || value.front() == '/' || value.find('\\') != std::string::npos) {
        return false;
    }
    size_t start = 0;
    while (start < value.size()) {
        const size_t end = value.find('/', start);
        const std::string component = value.substr(start, end == std::string::npos ? end : end - start);
        if (component.empty() || component == "." || component == "..") {
            return false;
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return true;
}

struct PackageResolution {
    std::string path;
    uint64_t generation = 0;
    bool override_used = false;
    bool found = false;
    std::string error;
};

inline bool package_path_within_root(const std::filesystem::path& root,
    const std::filesystem::path& candidate) {
    std::error_code error;
    const std::filesystem::path canonical_root = std::filesystem::weakly_canonical(root, error);
    if (error) return false;
    const std::filesystem::path canonical_candidate = std::filesystem::weakly_canonical(candidate, error);
    if (error) return false;
    auto root_part = canonical_root.begin();
    auto candidate_part = canonical_candidate.begin();
    for (; root_part != canonical_root.end(); ++root_part, ++candidate_part) {
        if (candidate_part == canonical_candidate.end() || *root_part != *candidate_part) return false;
    }
    return candidate_part != canonical_candidate.end();
}

inline PackageResolution resolve_package_path(const std::filesystem::path& base_root,
    const std::vector<std::filesystem::path>& override_roots, const std::string& logical_name,
    uint64_t generation) {
    PackageResolution result;
    if (!safe_package_path(logical_name)) {
        result.error = "unsafe package name";
        return result;
    }
    if (generation == 0 || override_roots.size() > 16) {
        result.error = "invalid package generation or override count";
        return result;
    }
    const auto candidate = [&logical_name](const std::filesystem::path& root) {
        return (root / std::filesystem::path(logical_name)).lexically_normal();
    };
    for (const auto& root : override_roots) {
        const std::filesystem::path path = candidate(root);
        if (std::filesystem::is_regular_file(path)) {
            if (!package_path_within_root(root, path)) {
                result.error = "package resolves outside mount root";
                return result;
            }
            result.path = path.string();
            result.generation = generation;
            result.override_used = true;
            result.found = true;
            return result;
        }
    }
    const std::filesystem::path path = candidate(base_root);
    if (std::filesystem::is_regular_file(path)) {
        if (!package_path_within_root(base_root, path)) {
            result.error = "package resolves outside mount root";
            return result;
        }
        result.path = path.string();
        result.generation = generation;
        result.found = true;
        return result;
    }
    result.error = "package is missing";
    return result;
}

inline PackageIndex parse_package_index(const std::string& bytes) {
    PackageIndex package;
    if (bytes.size() > PackageIndex::MAX_PACKAGE_BYTES) {
        package.error = "package exceeds size bound"; return package;
    }
    size_t offset = 0;
    while (offset < bytes.size()) {
        const size_t newline = bytes.find('\n', offset);
        const size_t end = newline == std::string::npos ? bytes.size() : newline;
        if (end - offset > PackageIndex::MAX_LINE_BYTES) { package.error = "section exceeds size bound"; return package; }
        const std::string line = bytes.substr(offset, end - offset);
        const size_t separator = line.find('=');
        if (separator == std::string::npos || separator == 0) { package.error = "malformed section"; return package; }
        const std::string key = line.substr(0, separator);
        if (key.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_") != std::string::npos ||
            package.sections.count(key) != 0 || package.sections.size() >= PackageIndex::MAX_SECTIONS) {
            package.error = "invalid or duplicate section"; return package;
        }
        package.sections.emplace(key, line.substr(separator + 1));
        if (newline == std::string::npos) break;
        offset = newline + 1;
    }
    const auto source = package.sections.find("source");
    if (source != package.sections.end() && !safe_package_path(source->second)) {
        package.error = "unsafe package path"; return package;
    }
    package.valid = true;
    return package;
}

inline PackageIndex read_package_index(const std::string& path) {
    PackageIndex package;
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) { package.error = "missing package"; return package; }
    const std::streamoff size = input.tellg();
    if (size < 0 || static_cast<size_t>(size) > PackageIndex::MAX_PACKAGE_BYTES) {
        package.error = "package exceeds size bound"; return package;
    }
    input.seekg(0);
    std::string bytes(static_cast<size_t>(size), '\0');
    input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!input && !bytes.empty()) { package.error = "package read failed"; return package; }
    return parse_package_index(bytes);
}

struct BinaryPackageSection {
    std::string name;
    uint64_t offset = 0;
    uint64_t size = 0;
    uint64_t unpacked_size = 0;
    uint32_t compression = 0;
    uint32_t checksum = 0;
};

struct BinaryPackageIndex {
    static constexpr size_t HEADER_BYTES = 32;
    static constexpr size_t ENTRY_BYTES = 48;
    static constexpr size_t MAX_UNPACKED_BYTES = 64 * 1024 * 1024;
    static constexpr int MAX_ZSTD_WINDOW_LOG = 26; // 64 MiB, matching MAX_UNPACKED_BYTES.
    std::vector<BinaryPackageSection> sections;
    bool valid = false;
    std::string error;
};

inline uint32_t package_crc32(const uint8_t* bytes, size_t size) {
    static const std::array<uint32_t, 256> table = []() {
        std::array<uint32_t, 256> values{};
        for (uint32_t index = 0; index < values.size(); ++index) {
            uint32_t value = index;
            for (uint32_t bit = 0; bit < 8; ++bit) {
                value = (value >> 1) ^ (0xEDB88320u & (0u - (value & 1u)));
            }
            values[index] = value;
        }
        return values;
    }();
    uint32_t checksum = 0xFFFFFFFFu;
    for (size_t index = 0; index < size; ++index) {
        checksum = table[(checksum ^ bytes[index]) & 0xFFu] ^ (checksum >> 8);
    }
    return ~checksum;
}

inline uint16_t package_u16(const std::vector<uint8_t>& bytes, size_t offset) {
    return static_cast<uint16_t>(bytes[offset]) | static_cast<uint16_t>(bytes[offset + 1] << 8);
}

inline uint64_t package_u64(const std::vector<uint8_t>& bytes, size_t offset) {
    uint64_t value = 0;
    for (size_t index = 0; index < 8; ++index) value |= static_cast<uint64_t>(bytes[offset + index]) << (index * 8);
    return value;
}

inline BinaryPackageIndex read_binary_package_index(const std::string& path) {
    BinaryPackageIndex package;
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) { package.error = "missing binary package"; return package; }
    const std::streamoff stream_size = input.tellg();
    if (stream_size < 0 || static_cast<uint64_t>(stream_size) > PackageIndex::MAX_PACKAGE_BYTES) {
        package.error = "binary package exceeds size bound"; return package;
    }
    std::vector<uint8_t> header(BinaryPackageIndex::HEADER_BYTES);
    input.seekg(0);
    input.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
    if (!input || header[0] != 'E' || header[1] != 'L' ||
        header[2] != 'P' || header[3] != 'K' || package_u16(header, 4) != 1) {
        package.error = "binary package header rejected"; return package;
    }
    const uint16_t count = package_u16(header, 6);
    const uint64_t index_offset = package_u64(header, 8);
    const uint64_t index_size = package_u64(header, 16);
    if (count == 0 || count > PackageIndex::MAX_SECTIONS || index_offset < BinaryPackageIndex::HEADER_BYTES ||
        index_size != static_cast<uint64_t>(count) * BinaryPackageIndex::ENTRY_BYTES ||
        index_offset > static_cast<uint64_t>(stream_size) ||
        index_size > static_cast<uint64_t>(stream_size) - index_offset) {
        package.error = "binary package index bounds rejected"; return package;
    }
    std::vector<uint8_t> bytes(static_cast<size_t>(index_size));
    input.seekg(static_cast<std::streamoff>(index_offset));
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input && !bytes.empty()) { package.error = "binary package index read failed"; return package; }
    const uint64_t index_end = index_offset + index_size;
    std::set<std::string> names;
    struct Range { uint64_t start; uint64_t end; };
    std::vector<Range> ranges;
    for (uint16_t index = 0; index < count; ++index) {
        const size_t entry = static_cast<size_t>(index) * BinaryPackageIndex::ENTRY_BYTES;
        size_t name_end = entry;
        while (name_end < entry + 16 && bytes[name_end] != 0) ++name_end;
        if (name_end == entry || name_end == entry + 16) { package.error = "binary section name rejected"; return package; }
        const std::string name(reinterpret_cast<const char*>(bytes.data() + entry), name_end - entry);
        if (!names.insert(name).second) { package.error = "duplicate binary section"; return package; }
        BinaryPackageSection section;
        section.name = name;
        section.offset = package_u64(bytes, entry + 16);
        section.size = package_u64(bytes, entry + 24);
        section.unpacked_size = package_u64(bytes, entry + 32);
        section.compression = static_cast<uint32_t>(bytes[entry + 40]) |
            (static_cast<uint32_t>(bytes[entry + 41]) << 8) |
            (static_cast<uint32_t>(bytes[entry + 42]) << 16) |
            (static_cast<uint32_t>(bytes[entry + 43]) << 24);
        section.checksum = static_cast<uint32_t>(bytes[entry + 44]) |
            (static_cast<uint32_t>(bytes[entry + 45]) << 8) |
            (static_cast<uint32_t>(bytes[entry + 46]) << 16) |
            (static_cast<uint32_t>(bytes[entry + 47]) << 24);
        if (section.compression > 1 || section.unpacked_size > BinaryPackageIndex::MAX_UNPACKED_BYTES ||
            section.offset % 16 != 0 || section.offset < index_end ||
            section.offset > static_cast<uint64_t>(stream_size) ||
            section.size > static_cast<uint64_t>(stream_size) - section.offset || section.size == 0) {
            package.error = "binary section bounds or compression rejected"; return package;
        }
        ranges.push_back({section.offset, section.offset + section.size});
        package.sections.push_back(section);
    }
    std::sort(ranges.begin(), ranges.end(), [](const Range& left, const Range& right) { return left.start < right.start; });
    for (size_t index = 1; index < ranges.size(); ++index) {
        if (ranges[index - 1].end > ranges[index].start) { package.error = "overlapping binary sections"; return package; }
    }
    package.valid = true;
    return package;
}

inline bool read_binary_package_section(const std::string& path, const BinaryPackageIndex& index,
    const std::string& name, std::vector<uint8_t>& output, std::string& error,
    BinaryPackageReadCancellationCheck cancellation_check = {}) {
    output.clear();
    if (!index.valid) { error = "binary package index is invalid"; return false; }
    const auto section_it = std::find_if(index.sections.begin(), index.sections.end(),
        [&name](const BinaryPackageSection& section) { return section.name == name; });
    if (section_it == index.sections.end()) { error = "binary section is missing"; return false; }
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) { error = "binary package is missing"; return false; }
    const std::streamoff stream_size = input.tellg();
    if (stream_size < 0 || static_cast<uint64_t>(stream_size) > PackageIndex::MAX_PACKAGE_BYTES ||
        section_it->offset > static_cast<uint64_t>(stream_size) ||
        section_it->size > static_cast<uint64_t>(stream_size) - section_it->offset) {
        error = "binary section read exceeds package"; return false;
    }
    input.seekg(static_cast<std::streamoff>(section_it->offset));
    size_t bytes_read = 0;
    if (section_it->compression == 0) {
        if (section_it->size != section_it->unpacked_size) { error = "raw section size mismatch"; return false; }
        output.resize(static_cast<size_t>(section_it->unpacked_size));
        while (bytes_read < output.size()) {
            if (cancellation_check && cancellation_check(bytes_read, output.size())) {
                output.clear(); error = "binary section read cancelled"; return false;
            }
            const size_t chunk = std::min(BINARY_PACKAGE_READ_CHUNK_BYTES, output.size() - bytes_read);
            input.read(reinterpret_cast<char*>(output.data() + bytes_read), static_cast<std::streamsize>(chunk));
            if (input.gcount() != static_cast<std::streamsize>(chunk)) {
                output.clear(); error = "binary section read failed"; return false;
            }
            bytes_read += chunk;
        }
        if (cancellation_check && cancellation_check(bytes_read, output.size())) {
            output.clear(); error = "binary section read cancelled"; return false;
        }
    } else {
        const size_t expected_size = static_cast<size_t>(section_it->unpacked_size);
        std::unique_ptr<ZSTD_DStream, decltype(&ZSTD_freeDStream)> decoder(
            ZSTD_createDStream(), ZSTD_freeDStream);
        if (!decoder || ZSTD_isError(ZSTD_DCtx_setParameter(
                decoder.get(), ZSTD_d_windowLogMax, BinaryPackageIndex::MAX_ZSTD_WINDOW_LOG)) ||
            ZSTD_isError(ZSTD_initDStream(decoder.get()))) {
            error = "zstd section decompression failed"; return false;
        }
        std::vector<uint8_t> encoded(BINARY_PACKAGE_READ_CHUNK_BYTES);
        std::array<uint8_t, BINARY_PACKAGE_READ_CHUNK_BYTES> decoded{};
        output.reserve(expected_size);
        bool frame_complete = false;
        while (bytes_read < section_it->size) {
            if (cancellation_check && cancellation_check(bytes_read, section_it->size)) {
                output.clear(); error = "binary section read cancelled"; return false;
            }
            const size_t chunk = std::min(BINARY_PACKAGE_READ_CHUNK_BYTES,
                static_cast<size_t>(section_it->size) - bytes_read);
            input.read(reinterpret_cast<char*>(encoded.data()), static_cast<std::streamsize>(chunk));
            if (input.gcount() != static_cast<std::streamsize>(chunk)) {
                output.clear(); error = "binary section read failed"; return false;
            }
            bytes_read += chunk;
            ZSTD_inBuffer source{encoded.data(), chunk, 0};
            bool drain = false;
            while (source.pos < source.size || drain) {
                if (cancellation_check && cancellation_check(bytes_read, section_it->size)) {
                    output.clear(); error = "binary section decompression cancelled"; return false;
                }
                const size_t remaining = expected_size - output.size();
                const size_t capacity = std::min(BINARY_PACKAGE_READ_CHUNK_BYTES,
                    std::max<size_t>(remaining, 1));
                ZSTD_outBuffer destination{decoded.data(), capacity, 0};
                const size_t previous_source = source.pos;
                const size_t result = ZSTD_decompressStream(decoder.get(), &destination, &source);
                if (ZSTD_isError(result) || destination.pos > remaining) {
                    output.clear(); error = "zstd section decompression failed"; return false;
                }
                output.insert(output.end(), decoded.begin(), decoded.begin() + destination.pos);
                if (result == 0) {
                    if (source.pos != source.size || bytes_read != section_it->size) {
                        output.clear(); error = "zstd section decompression failed"; return false;
                    }
                    frame_complete = true;
                    break;
                }
                drain = destination.pos == destination.size;
                if (source.pos == previous_source && destination.pos == 0) {
                    if (source.pos == source.size) break;
                    output.clear(); error = "zstd section decompression failed"; return false;
                }
                if (source.pos == source.size && !drain) break;
            }
            if (frame_complete) break;
        }
        if (!frame_complete) {
            output.clear(); error = "zstd section decompression failed"; return false;
        }
        if (cancellation_check && cancellation_check(bytes_read, section_it->size)) {
            output.clear(); error = "binary section decompression cancelled"; return false;
        }
    }
    if (package_crc32(output.data(), output.size()) != section_it->checksum) {
        output.clear(); error = "binary section checksum mismatch"; return false;
    }
    return true;
}

} // namespace probe
