#pragma once

#include "virtual_package.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace probe {

inline void put_package_u16(std::vector<uint8_t>& bytes, size_t offset, uint16_t value) {
    bytes[offset] = static_cast<uint8_t>(value);
    bytes[offset + 1] = static_cast<uint8_t>(value >> 8);
}

inline void put_package_u32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
    for (size_t index = 0; index < 4; ++index) bytes[offset + index] = static_cast<uint8_t>(value >> (index * 8));
}

inline void put_package_u64(std::vector<uint8_t>& bytes, size_t offset, uint64_t value) {
    for (size_t index = 0; index < 8; ++index) bytes[offset + index] = static_cast<uint8_t>(value >> (index * 8));
}

inline bool write_zstd_streaming_fixture(const std::filesystem::path& path, size_t payload_size,
    bool truncate_frame = false) {
    const std::vector<uint8_t> payload(payload_size, 0);
    std::vector<uint8_t> encoded(ZSTD_compressBound(payload.size()));
    const size_t encoded_size = ZSTD_compress(encoded.data(), encoded.size(), payload.data(), payload.size(), 1);
    if (ZSTD_isError(encoded_size)) return false;
    encoded.resize(encoded_size);
    if (truncate_frame && !encoded.empty()) encoded.pop_back();

    const size_t index_end = BinaryPackageIndex::HEADER_BYTES + BinaryPackageIndex::ENTRY_BYTES;
    std::vector<uint8_t> bytes(index_end + encoded.size(), 0);
    bytes[0] = 'E'; bytes[1] = 'L'; bytes[2] = 'P'; bytes[3] = 'K';
    put_package_u16(bytes, 4, 1);
    put_package_u16(bytes, 6, 1);
    put_package_u64(bytes, 8, BinaryPackageIndex::HEADER_BYTES);
    put_package_u64(bytes, 16, BinaryPackageIndex::ENTRY_BYTES);
    const size_t entry = BinaryPackageIndex::HEADER_BYTES;
    const std::string name = "payload";
    std::copy(name.begin(), name.end(), bytes.begin() + entry);
    put_package_u64(bytes, entry + 16, index_end);
    put_package_u64(bytes, entry + 24, encoded.size());
    put_package_u64(bytes, entry + 32, payload.size());
    put_package_u32(bytes, entry + 40, 1);
    put_package_u32(bytes, entry + 44, package_crc32(payload.data(), payload.size()));
    std::copy(encoded.begin(), encoded.end(), bytes.begin() + index_end);
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return bool(output);
}

inline bool probe_zstd_streaming(const std::filesystem::path& root) {
    constexpr size_t DECODE_BYTES = size_t(4) * 1024 * 1024;
    const std::filesystem::path valid_path = root / "streaming.elpk";
    if (!write_zstd_streaming_fixture(valid_path, DECODE_BYTES)) return false;
    const BinaryPackageIndex valid_index = read_binary_package_index(valid_path.string());
    std::vector<uint8_t> decoded;
    std::string error;
    const bool valid = valid_index.valid && read_binary_package_section(
        valid_path.string(), valid_index, "payload", decoded, error) && decoded.size() == DECODE_BYTES &&
        std::all_of(decoded.begin(), decoded.end(), [](uint8_t byte) { return byte == 0; });

    const std::filesystem::path truncated_path = root / "streaming-truncated.elpk";
    if (!write_zstd_streaming_fixture(truncated_path, DECODE_BYTES, true)) return false;
    const BinaryPackageIndex truncated_index = read_binary_package_index(truncated_path.string());
    std::vector<uint8_t> truncated;
    std::string truncated_error;
    const bool truncation_rejected = truncated_index.valid && !read_binary_package_section(
        truncated_path.string(), truncated_index, "payload", truncated, truncated_error) &&
        truncated.empty() && truncated_error == "zstd section decompression failed";

    std::vector<uint8_t> cancelled;
    std::string cancellation_error;
    size_t decode_checkpoints = 0;
    const bool cancellation_discarded_output = valid_index.valid && !read_binary_package_section(
        valid_path.string(), valid_index, "payload", cancelled, cancellation_error,
        [&decode_checkpoints](size_t bytes_read, size_t) {
            if (bytes_read == 0) return false;
            ++decode_checkpoints;
            return decode_checkpoints == 3;
        }) && decode_checkpoints == 3 && cancelled.empty() &&
        cancellation_error == "binary section decompression cancelled";
    return valid && truncation_rejected && cancellation_discarded_output;
}

} // namespace probe
