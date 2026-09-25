#pragma once

// Versioned, bounded disk cache for cooked Jolt collision shapes. Cache
// entries are disposable: every read failure is a cache miss, never an asset
// load failure.
#include "coordinate_abi.h"
#include "sha256_file.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace elisa::physics::shape_cache {

inline constexpr uint32_t FORMAT_VERSION = 1;
inline constexpr size_t SHA256_HEX_BYTES = 64;
inline constexpr size_t MAGIC_BYTES = 8;
inline constexpr size_t FORMAT_VERSION_OFFSET = MAGIC_BYTES;
inline constexpr size_t SHAPE_KIND_OFFSET = FORMAT_VERSION_OFFSET + sizeof(uint32_t);
inline constexpr size_t JOLT_VERSION_OFFSET = SHAPE_KIND_OFFSET + sizeof(uint32_t);
inline constexpr size_t PAYLOAD_SIZE_OFFSET = JOLT_VERSION_OFFSET + sizeof(uint64_t);
inline constexpr size_t PAYLOAD_CRC_OFFSET = PAYLOAD_SIZE_OFFSET + sizeof(uint64_t);
inline constexpr size_t KEY_OFFSET = PAYLOAD_CRC_OFFSET + sizeof(uint32_t);
inline constexpr size_t HEADER_BYTES = KEY_OFFSET + SHA256_HEX_BYTES;
inline constexpr size_t MAX_PAYLOAD_BYTES = 16u * 1024u * 1024u;
inline constexpr std::array<uint8_t, MAGIC_BYTES> MAGIC = {'E', 'L', 'J', 'S', 'H', 'A', 'P', 'E'};
inline constexpr size_t CONFIG_KIND_OFFSET = 0;
inline constexpr size_t CONFIG_SCALE_X_OFFSET = CONFIG_KIND_OFFSET + sizeof(uint32_t);
inline constexpr size_t CONFIG_SCALE_Y_OFFSET = CONFIG_SCALE_X_OFFSET + sizeof(uint32_t);
inline constexpr size_t CONFIG_SCALE_Z_OFFSET = CONFIG_SCALE_Y_OFFSET + sizeof(uint32_t);
inline constexpr size_t CONFIG_PROFILE_ABI_OFFSET = CONFIG_SCALE_Z_OFFSET + sizeof(uint32_t);
inline constexpr size_t CONFIG_PROFILE_MATRIX_OFFSET = CONFIG_PROFILE_ABI_OFFSET + sizeof(uint32_t);
inline constexpr size_t CONFIG_PROFILE_HANDEDNESS_OFFSET = CONFIG_PROFILE_MATRIX_OFFSET + sizeof(uint32_t);
inline constexpr size_t CONFIG_PROFILE_DEPTH_OFFSET = CONFIG_PROFILE_HANDEDNESS_OFFSET + sizeof(uint32_t);
inline constexpr size_t CONFIG_PROFILE_QUAT_OFFSET = CONFIG_PROFILE_DEPTH_OFFSET + sizeof(uint32_t);
inline constexpr size_t CONFIG_PROFILE_TANGENT_OFFSET = CONFIG_PROFILE_QUAT_OFFSET + sizeof(uint32_t);
inline constexpr size_t CONFIG_PROFILE_METRES_OFFSET = CONFIG_PROFILE_TANGENT_OFFSET + sizeof(uint32_t);
inline constexpr size_t CONFIG_PROFILE_RESERVED_OFFSET = CONFIG_PROFILE_METRES_OFFSET + sizeof(uint32_t);
inline constexpr size_t CONFIGURATION_BYTES = CONFIG_PROFILE_RESERVED_OFFSET + sizeof(uint32_t);

inline void write_u32(uint8_t* output, uint32_t value) {
    for (size_t index = 0; index < sizeof(uint32_t); ++index) output[index] = uint8_t(value >> (index * 8));
}

inline void write_u64(uint8_t* output, uint64_t value) {
    for (size_t index = 0; index < sizeof(uint64_t); ++index) output[index] = uint8_t(value >> (index * 8));
}

inline uint32_t read_u32(const uint8_t* input) {
    uint32_t value = 0;
    for (size_t index = 0; index < sizeof(uint32_t); ++index) value |= uint32_t(input[index]) << (index * 8);
    return value;
}

inline uint64_t read_u64(const uint8_t* input) {
    uint64_t value = 0;
    for (size_t index = 0; index < sizeof(uint64_t); ++index) value |= uint64_t(input[index]) << (index * 8);
    return value;
}

inline std::string digest(const uint8_t* bytes, size_t size) {
    assets::Sha256 hash;
    hash.update(bytes, size);
    return hash.finish();
}

inline bool valid_digest(const std::string& value) {
    return value.size() == SHA256_HEX_BYTES &&
        value.find_first_not_of("0123456789abcdef") == std::string::npos;
}

inline bool encode_configuration(int32_t shape_kind, float scale_x, float scale_y,
        float scale_z, const ElisaCoordinateProfile& profile,
        std::array<uint8_t, CONFIGURATION_BYTES>& fields) {
    if (!std::isfinite(scale_x) ||
        !std::isfinite(scale_y) || !std::isfinite(scale_z) ||
        !elisa_coordinate_profile_valid(&profile)) return false;
    fields = {};
    write_u32(fields.data() + CONFIG_KIND_OFFSET, uint32_t(shape_kind));
    uint32_t scale_bits = 0;
    std::memcpy(&scale_bits, &scale_x, sizeof(scale_bits));
    write_u32(fields.data() + CONFIG_SCALE_X_OFFSET, scale_bits);
    std::memcpy(&scale_bits, &scale_y, sizeof(scale_bits));
    write_u32(fields.data() + CONFIG_SCALE_Y_OFFSET, scale_bits);
    std::memcpy(&scale_bits, &scale_z, sizeof(scale_bits));
    write_u32(fields.data() + CONFIG_SCALE_Z_OFFSET, scale_bits);
    write_u32(fields.data() + CONFIG_PROFILE_ABI_OFFSET, profile.abi_version);
    write_u32(fields.data() + CONFIG_PROFILE_MATRIX_OFFSET, profile.matrix_order);
    write_u32(fields.data() + CONFIG_PROFILE_HANDEDNESS_OFFSET, profile.handedness);
    write_u32(fields.data() + CONFIG_PROFILE_DEPTH_OFFSET, profile.depth_range);
    write_u32(fields.data() + CONFIG_PROFILE_QUAT_OFFSET, profile.quaternion_layout);
    write_u32(fields.data() + CONFIG_PROFILE_TANGENT_OFFSET, uint32_t(profile.tangent_parity));
    std::memcpy(&scale_bits, &profile.metres_per_unit, sizeof(scale_bits));
    write_u32(fields.data() + CONFIG_PROFILE_METRES_OFFSET, scale_bits);
    write_u32(fields.data() + CONFIG_PROFILE_RESERVED_OFFSET, profile.reserved);
    return true;
}

inline bool make_cache_key(const std::string& source_digest, int32_t shape_kind,
        float scale_x, float scale_y, float scale_z, const ElisaCoordinateProfile& profile,
        uint64_t jolt_version, std::string& key) {
    if (!valid_digest(source_digest)) return false;
    std::array<uint8_t, CONFIGURATION_BYTES> fields{};
    if (!encode_configuration(shape_kind, scale_x, scale_y, scale_z, profile, fields)) return false;

    constexpr char domain[] = "elisa-jolt-shape-cache-key-v1";
    assets::Sha256 hash;
    hash.update(reinterpret_cast<const uint8_t*>(domain), sizeof(domain) - 1);
    hash.update(reinterpret_cast<const uint8_t*>(source_digest.data()), source_digest.size());
    hash.update(fields.data(), fields.size());
    std::array<uint8_t, sizeof(uint64_t)> version{};
    write_u64(version.data(), jolt_version);
    hash.update(version.data(), version.size());
    key = hash.finish();
    return true;
}

inline bool make_cache_path(const std::filesystem::path& project_root,
        const std::filesystem::path& canonical_asset, int32_t shape_kind,
        float scale_x, float scale_y, float scale_z,
        const ElisaCoordinateProfile& profile, std::filesystem::path& result) {
    const std::filesystem::path relative = canonical_asset.lexically_relative(project_root);
    if (relative.empty() || relative.is_absolute()) return false;
    for (const auto& component : relative) if (component == "..") return false;
    const std::string relative_name = relative.generic_string();
    std::array<uint8_t, CONFIGURATION_BYTES> fields{};
    if (!encode_configuration(shape_kind, scale_x, scale_y, scale_z, profile, fields)) return false;
    assets::Sha256 hash;
    constexpr char domain[] = "elisa-jolt-shape-cache-path-v1";
    hash.update(reinterpret_cast<const uint8_t*>(domain), sizeof(domain) - 1);
    std::array<uint8_t, sizeof(uint64_t)> path_length{};
    write_u64(path_length.data(), relative_name.size());
    hash.update(path_length.data(), path_length.size());
    hash.update(reinterpret_cast<const uint8_t*>(relative_name.data()), relative_name.size());
    hash.update(fields.data(), fields.size());
    const std::string name = hash.finish();
    result = project_root / "build" / "cache" / "physics" / (name + ".joltshape");
    return true;
}

inline bool load(const std::filesystem::path& path, const std::string& key,
        int32_t shape_kind, uint64_t jolt_version, std::vector<uint8_t>& payload) {
    if (!valid_digest(key)) return false;
    std::error_code error;
    const uint64_t file_size = std::filesystem::file_size(path, error);
    if (error || file_size < HEADER_BYTES || file_size > HEADER_BYTES + MAX_PAYLOAD_BYTES) return false;
    std::vector<uint8_t> bytes(static_cast<size_t>(file_size));
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input || !std::equal(MAGIC.begin(), MAGIC.end(), bytes.begin()) ||
        read_u32(bytes.data() + FORMAT_VERSION_OFFSET) != FORMAT_VERSION ||
        read_u32(bytes.data() + SHAPE_KIND_OFFSET) != uint32_t(shape_kind) ||
        read_u64(bytes.data() + JOLT_VERSION_OFFSET) != jolt_version ||
        read_u64(bytes.data() + PAYLOAD_SIZE_OFFSET) != file_size - HEADER_BYTES ||
        std::memcmp(bytes.data() + KEY_OFFSET, key.data(), key.size()) != 0) return false;
    const uint8_t* data = bytes.data() + HEADER_BYTES;
    const size_t size = bytes.size() - HEADER_BYTES;
    if (probe::package_crc32(data, size) != read_u32(bytes.data() + PAYLOAD_CRC_OFFSET)) return false;
    payload.assign(data, data + size);
    return !payload.empty();
}

inline bool store(const std::filesystem::path& path, const std::string& key,
        int32_t shape_kind, uint64_t jolt_version, const std::vector<uint8_t>& payload) {
    if (!valid_digest(key) || payload.empty() || payload.size() > MAX_PAYLOAD_BYTES) return false;
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) return false;

    std::vector<uint8_t> bytes(HEADER_BYTES + payload.size());
    std::copy(MAGIC.begin(), MAGIC.end(), bytes.begin());
    write_u32(bytes.data() + FORMAT_VERSION_OFFSET, FORMAT_VERSION);
    write_u32(bytes.data() + SHAPE_KIND_OFFSET, uint32_t(shape_kind));
    write_u64(bytes.data() + JOLT_VERSION_OFFSET, jolt_version);
    write_u64(bytes.data() + PAYLOAD_SIZE_OFFSET, payload.size());
    write_u32(bytes.data() + PAYLOAD_CRC_OFFSET, probe::package_crc32(payload.data(), payload.size()));
    std::memcpy(bytes.data() + KEY_OFFSET, key.data(), key.size());
    std::copy(payload.begin(), payload.end(), bytes.begin() + HEADER_BYTES);

    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count() ^
        int64_t(std::hash<std::thread::id>{}(std::this_thread::get_id()));
    const std::filesystem::path temporary = path.string() + ".tmp-" + std::to_string(nonce);
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) return false;
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        output.flush();
        if (!output) {
            output.close();
            std::filesystem::remove(temporary, error);
            return false;
        }
    }
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        return false;
    }
    return true;
}

} // namespace elisa::physics::shape_cache
