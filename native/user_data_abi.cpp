#include "user_data_abi.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <string>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#define ELISA_USER_DATA_PID _getpid
#else
#include <unistd.h>
#define ELISA_USER_DATA_PID getpid
#endif

namespace {

constexpr uint32_t ABI_VERSION = 1;
constexpr uint32_t FILE_MAGIC = 0x44554C45; // ELUD, encoded little-endian.
constexpr uint32_t FILE_VERSION = 1;
constexpr uint32_t MAX_FIELDS = ELISA_USER_DATA_MAX_FIELDS;
constexpr size_t MAX_FILE_BYTES = 4 + 4 + 8 + 4 + 4 + MAX_FIELDS * 8 + 8;
constexpr uint64_t FNV_OFFSET = 14695981039346656037ULL;
constexpr uint64_t FNV_PRIME = 1099511628211ULL;

struct ServiceState {
    std::mutex mutex;
    bool initialized = false;
    std::filesystem::path root;
    std::string root_utf8;
    std::atomic<uint64_t> temporary_serial{1};
};

ServiceState& service() {
    static ServiceState value;
    return value;
}

bool valid_component(const char* value, size_t maximum, bool application_id) {
    if (value == nullptr) return false;
    const std::string text(value);
    if (text.empty() || text.size() > maximum || text == "." || text == "..") return false;
    if (application_id && (text.front() == '.' || text.back() == '.')) return false;
    for (unsigned char character : text) {
        const bool valid = (character >= 'a' && character <= 'z') ||
            (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') || character == '.' ||
            character == '_' || character == '-';
        if (!valid) return false;
    }
    return true;
}

bool user_data_base(std::filesystem::path& base) {
    const char* override_path = std::getenv("ELISA_USER_DATA_DIR");
    if (override_path != nullptr && override_path[0] != '\0') {
        std::error_code error;
        const std::filesystem::path candidate = std::filesystem::u8path(override_path);
        if (!candidate.is_absolute()) return false;
        base = candidate.lexically_normal();
        return base.native().size() < 4096;
    }

#if defined(_WIN32)
    const char* appdata = std::getenv("APPDATA");
    if (appdata == nullptr || appdata[0] == '\0') return false;
    base = std::filesystem::u8path(appdata) / "Elisa";
#elif defined(__APPLE__)
    const char* home = std::getenv("HOME");
    if (home == nullptr || home[0] == '\0') return false;
    base = std::filesystem::u8path(home) / "Library" / "Application Support" / "Elisa";
#else
    const char* data_home = std::getenv("XDG_DATA_HOME");
    if (data_home != nullptr && data_home[0] != '\0') {
        base = std::filesystem::u8path(data_home) / "elisa";
    } else {
        const char* home = std::getenv("HOME");
        if (home == nullptr || home[0] == '\0') return false;
        base = std::filesystem::u8path(home) / ".local" / "share" / "elisa";
    }
#endif
    return base.is_absolute() && base.native().size() < 4096;
}

void append_u32(std::vector<uint8_t>& bytes, uint32_t value) {
    for (uint32_t shift = 0; shift < 32; shift += 8) {
        bytes.push_back(static_cast<uint8_t>(value >> shift));
    }
}

void append_u64(std::vector<uint8_t>& bytes, uint64_t value) {
    for (uint32_t shift = 0; shift < 64; shift += 8) {
        bytes.push_back(static_cast<uint8_t>(value >> shift));
    }
}

bool read_u32(const std::vector<uint8_t>& bytes, size_t& offset, uint32_t& value) {
    if (offset > bytes.size() || bytes.size() - offset < 4) return false;
    value = 0;
    for (uint32_t index = 0; index < 4; ++index) {
        value |= static_cast<uint32_t>(bytes[offset + index]) << (index * 8);
    }
    offset += 4;
    return true;
}

bool read_u64(const std::vector<uint8_t>& bytes, size_t& offset, uint64_t& value) {
    if (offset > bytes.size() || bytes.size() - offset < 8) return false;
    value = 0;
    for (uint32_t index = 0; index < 8; ++index) {
        value |= static_cast<uint64_t>(bytes[offset + index]) << (index * 8);
    }
    offset += 8;
    return true;
}

uint64_t checksum(const uint8_t* bytes, size_t length) {
    uint64_t value = FNV_OFFSET;
    for (size_t index = 0; index < length; ++index) {
        value ^= bytes[index];
        value *= FNV_PRIME;
    }
    return value;
}

bool replace_file(const std::filesystem::path& source, const std::filesystem::path& target) {
#if defined(_WIN32)
    return MoveFileExW(source.c_str(), target.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    return std::rename(source.string().c_str(), target.string().c_str()) == 0;
#endif
}

int32_t read_bytes(const std::filesystem::path& path, std::vector<uint8_t>& bytes) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error == std::errc::no_such_file_or_directory) return ELISA_USER_DATA_NOT_FOUND;
    if (error) return ELISA_USER_DATA_IO_FAILURE;
    if (std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status)) {
        return ELISA_USER_DATA_CORRUPT;
    }
    const uintmax_t size = std::filesystem::file_size(path, error);
    if (error) return ELISA_USER_DATA_IO_FAILURE;
    if (size > MAX_FILE_BYTES || size < 40) return ELISA_USER_DATA_CORRUPT;
    bytes.resize(static_cast<size_t>(size));
    std::ifstream stream(path, std::ios::binary);
    if (!stream || !stream.read(reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()))) {
        return ELISA_USER_DATA_IO_FAILURE;
    }
    return ELISA_USER_DATA_OK;
}

} // namespace

extern "C" uint32_t elisa_user_data_abi_version() {
    return ABI_VERSION;
}

extern "C" int32_t elisa_user_data_v1_initialize(const char* application_id) {
    if (!valid_component(application_id, 96, true)) return ELISA_USER_DATA_INVALID_KEY;
    std::filesystem::path base;
    if (!user_data_base(base)) return ELISA_USER_DATA_IO_FAILURE;
    std::filesystem::path root = base / std::filesystem::u8path(application_id);
    if (root.native().size() >= 4096) return ELISA_USER_DATA_CAPACITY;
    std::error_code error;
    std::filesystem::create_directories(root, error);
    if (error || !std::filesystem::is_directory(root, error) || error) return ELISA_USER_DATA_IO_FAILURE;
    const std::string utf8 = root.u8string();
    if (utf8.empty() || utf8.size() >= 4096) return ELISA_USER_DATA_CAPACITY;

    ServiceState& state = service();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.root = std::move(root);
    state.root_utf8 = utf8;
    state.initialized = true;
    return ELISA_USER_DATA_OK;
}

extern "C" const char* elisa_user_data_v1_directory() {
    ServiceState& state = service();
    std::lock_guard<std::mutex> lock(state.mutex);
    return state.initialized ? state.root_utf8.c_str() : "";
}

extern "C" int32_t elisa_user_data_v1_write_blob(const char* key, int64_t version,
        const int64_t* fields, uint32_t count) {
    if (version <= 0 || fields == nullptr || count == 0 || count > MAX_FIELDS) {
        return ELISA_USER_DATA_INVALID_ARGUMENT;
    }
    ServiceState& state = service();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!state.initialized) return ELISA_USER_DATA_NOT_INITIALIZED;
    if (!valid_component(key, 48, false)) return ELISA_USER_DATA_INVALID_KEY;

    std::vector<uint8_t> bytes;
    bytes.reserve(32 + count * sizeof(int64_t));
    append_u32(bytes, FILE_MAGIC);
    append_u32(bytes, FILE_VERSION);
    append_u64(bytes, static_cast<uint64_t>(version));
    append_u32(bytes, count);
    append_u32(bytes, 0);
    for (uint32_t index = 0; index < count; ++index) {
        append_u64(bytes, static_cast<uint64_t>(fields[index]));
    }
    append_u64(bytes, checksum(bytes.data(), bytes.size()));

    const std::filesystem::path target = state.root / (std::string(key) + ".save");
    std::error_code filesystem_error;
    const auto target_status = std::filesystem::symlink_status(target, filesystem_error);
    if (!filesystem_error && std::filesystem::is_symlink(target_status)) return ELISA_USER_DATA_CORRUPT;
    if (filesystem_error != std::errc::no_such_file_or_directory && filesystem_error) {
        return ELISA_USER_DATA_IO_FAILURE;
    }

    const uint64_t serial = state.temporary_serial.fetch_add(1, std::memory_order_relaxed);
    const std::string temporary_name = std::string(key) + ".tmp-" +
        std::to_string(static_cast<long long>(ELISA_USER_DATA_PID())) + "-" +
        std::to_string(serial);
    const std::filesystem::path temporary = state.root / temporary_name;
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::out | std::ios::trunc);
        if (!stream) return ELISA_USER_DATA_IO_FAILURE;
        stream.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        stream.flush();
        if (!stream) {
            stream.close();
            std::filesystem::remove(temporary, filesystem_error);
            return ELISA_USER_DATA_IO_FAILURE;
        }
    }
    if (!replace_file(temporary, target)) {
        std::filesystem::remove(temporary, filesystem_error);
        return ELISA_USER_DATA_IO_FAILURE;
    }
    return ELISA_USER_DATA_OK;
}

extern "C" int32_t elisa_user_data_v1_read_blob(const char* key, int64_t* version,
        int64_t* fields, uint32_t capacity, uint32_t* count) {
    if (version == nullptr || fields == nullptr || count == nullptr || capacity == 0) {
        return ELISA_USER_DATA_INVALID_ARGUMENT;
    }
    ServiceState& state = service();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!state.initialized) return ELISA_USER_DATA_NOT_INITIALIZED;
    if (!valid_component(key, 48, false)) return ELISA_USER_DATA_INVALID_KEY;

    std::vector<uint8_t> bytes;
    const std::filesystem::path target = state.root / (std::string(key) + ".save");
    const int32_t loaded = read_bytes(target, bytes);
    if (loaded != ELISA_USER_DATA_OK) return loaded;

    size_t offset = 0;
    uint32_t magic = 0;
    uint32_t file_version = 0;
    uint64_t record_version = 0;
    uint32_t field_count = 0;
    uint32_t reserved = 0;
    if (!read_u32(bytes, offset, magic) || !read_u32(bytes, offset, file_version) ||
        !read_u64(bytes, offset, record_version) || !read_u32(bytes, offset, field_count) ||
        !read_u32(bytes, offset, reserved)) return ELISA_USER_DATA_CORRUPT;
    if (magic != FILE_MAGIC) return ELISA_USER_DATA_CORRUPT;
    if (file_version != FILE_VERSION) return ELISA_USER_DATA_WRONG_VERSION;
    if (record_version == 0 || record_version > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) ||
        field_count == 0 || field_count > MAX_FIELDS || reserved != 0) return ELISA_USER_DATA_CORRUPT;
    if (field_count > capacity) return ELISA_USER_DATA_CAPACITY;
    const size_t expected_size = offset + static_cast<size_t>(field_count) * 8 + 8;
    if (bytes.size() != expected_size) return ELISA_USER_DATA_CORRUPT;
    uint64_t stored_checksum = 0;
    const size_t checksum_offset = bytes.size() - 8;
    size_t checksum_reader = checksum_offset;
    if (!read_u64(bytes, checksum_reader, stored_checksum) ||
        checksum(bytes.data(), checksum_offset) != stored_checksum) {
        return ELISA_USER_DATA_CORRUPT;
    }

    std::array<int64_t, MAX_FIELDS> staged{};
    for (uint32_t index = 0; index < field_count; ++index) {
        uint64_t raw = 0;
        if (!read_u64(bytes, offset, raw)) return ELISA_USER_DATA_CORRUPT;
        std::memcpy(&staged[index], &raw, sizeof(raw));
    }
    std::memcpy(version, &record_version, sizeof(record_version));
    *count = field_count;
    for (uint32_t index = 0; index < capacity; ++index) {
        fields[index] = index < field_count ? staged[index] : 0;
    }
    return ELISA_USER_DATA_OK;
}

extern "C" int32_t elisa_user_data_v1_remove_blob(const char* key) {
    ServiceState& state = service();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!state.initialized) return ELISA_USER_DATA_NOT_INITIALIZED;
    if (!valid_component(key, 48, false)) return ELISA_USER_DATA_INVALID_KEY;
    const std::filesystem::path target = state.root / (std::string(key) + ".save");
    std::error_code error;
    const bool removed = std::filesystem::remove(target, error);
    if (error) return ELISA_USER_DATA_IO_FAILURE;
    return removed ? ELISA_USER_DATA_OK : ELISA_USER_DATA_NOT_FOUND;
}
