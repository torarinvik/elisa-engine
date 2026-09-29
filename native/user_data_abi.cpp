#include "user_data_abi.h"

#include <array>
#include <atomic>
#include <chrono>
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
#include <fcntl.h>
#include <unistd.h>
#define ELISA_USER_DATA_PID getpid
#endif

namespace {

constexpr uint32_t ABI_VERSION = 1;
constexpr uint32_t FILE_MAGIC = 0x44554C45; // ELUD, encoded little-endian.
constexpr uint32_t FILE_VERSION = 1;
constexpr uint32_t PAYLOAD_MAGIC = 0x54444C45; // ELDT, encoded little-endian.
constexpr uint32_t PAYLOAD_FILE_VERSION = 1;
constexpr uint32_t MAX_FIELDS = ELISA_USER_DATA_MAX_FIELDS;
constexpr size_t MAX_FILE_BYTES = 4 + 4 + 8 + 4 + 4 + MAX_FIELDS * 8 + 8;
constexpr size_t PAYLOAD_HEADER_BYTES = 4 + 4 + 8 + 4 + 4;
constexpr size_t MAX_PAYLOAD_FILE_BYTES = PAYLOAD_HEADER_BYTES +
    ELISA_USER_DATA_MAX_PAYLOAD_BYTES + 8;
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

int32_t read_bytes(const std::filesystem::path& path, std::vector<uint8_t>& bytes,
        size_t maximum, size_t minimum) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error == std::errc::no_such_file_or_directory) return ELISA_USER_DATA_NOT_FOUND;
    if (error) return ELISA_USER_DATA_IO_FAILURE;
    if (std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status)) {
        return ELISA_USER_DATA_CORRUPT;
    }
    const uintmax_t size = std::filesystem::file_size(path, error);
    if (error) return ELISA_USER_DATA_IO_FAILURE;
    if (size > maximum || size < minimum) return ELISA_USER_DATA_CORRUPT;
    bytes.resize(static_cast<size_t>(size));
    std::ifstream stream(path, std::ios::binary);
    if (!stream || !stream.read(reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()))) {
        return ELISA_USER_DATA_IO_FAILURE;
    }
    return ELISA_USER_DATA_OK;
}

std::filesystem::path backup_of(const std::filesystem::path& target) {
    std::filesystem::path backup = target;
    backup += ".bak";
    return backup;
}

// Both record formats end with an FNV-1a checksum over every earlier byte.
bool envelope_intact(const std::vector<uint8_t>& bytes) {
    if (bytes.size() < 16) return false;
    const size_t checksum_offset = bytes.size() - 8;
    size_t reader = checksum_offset;
    uint64_t stored = 0;
    return read_u64(bytes, reader, stored) && checksum(bytes.data(), checksum_offset) == stored;
}

// Reads the record, or the last good copy when a crash or bad sector left the
// primary missing or damaged. Only a fully checksummed backup is used.
int32_t read_recovering(const std::filesystem::path& target, std::vector<uint8_t>& bytes,
        size_t maximum, size_t minimum) {
    const int32_t primary = read_bytes(target, bytes, maximum, minimum);
    if (primary == ELISA_USER_DATA_OK && envelope_intact(bytes)) return primary;
    if (primary == ELISA_USER_DATA_IO_FAILURE) return primary;
    std::vector<uint8_t> previous;
    if (read_bytes(backup_of(target), previous, maximum, minimum) == ELISA_USER_DATA_OK &&
            envelope_intact(previous)) {
        bytes.swap(previous);
        return ELISA_USER_DATA_OK;
    }
    return primary;
}

// Writes and flushes the bytes to stable storage before the caller renames.
bool write_synced(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
#if defined(_WIN32)
    std::ofstream stream(path, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!stream) return false;
    stream.write(reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    stream.flush();
    return static_cast<bool>(stream);
#else
    const int descriptor = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (descriptor < 0) return false;
    size_t written = 0;
    bool ok = true;
    while (ok && written < bytes.size()) {
        const ssize_t count = ::write(descriptor, bytes.data() + written, bytes.size() - written);
        if (count <= 0) ok = false; else written += static_cast<size_t>(count);
    }
#if defined(F_FULLFSYNC)
    if (ok && ::fcntl(descriptor, F_FULLFSYNC) != 0 && ::fsync(descriptor) != 0) ok = false;
#else
    if (ok && ::fsync(descriptor) != 0) ok = false;
#endif
    return ::close(descriptor) == 0 && ok;
#endif
}

void sync_directory(const std::filesystem::path& directory) {
#if !defined(_WIN32)
    const int descriptor = ::open(directory.c_str(), O_RDONLY);
    if (descriptor < 0) return;
    ::fsync(descriptor);
    ::close(descriptor);
#else
    (void)directory;
#endif
}

int32_t write_atomic_file(ServiceState& state, const char* key,
        const std::filesystem::path& target, const std::vector<uint8_t>& bytes) {
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
    if (!write_synced(temporary, bytes)) {
        std::filesystem::remove(temporary, filesystem_error);
        return ELISA_USER_DATA_IO_FAILURE;
    }
    // Keep the previous intact record as the recovery copy. A crash between
    // the two renames leaves the backup, which readers fall back to.
    std::vector<uint8_t> previous;
    if (read_bytes(target, previous, MAX_PAYLOAD_FILE_BYTES, 16) == ELISA_USER_DATA_OK &&
            envelope_intact(previous) && !replace_file(target, backup_of(target))) {
        std::filesystem::remove(temporary, filesystem_error);
        return ELISA_USER_DATA_IO_FAILURE;
    }
    if (!replace_file(temporary, target)) {
        std::filesystem::remove(temporary, filesystem_error);
        return ELISA_USER_DATA_IO_FAILURE;
    }
    sync_directory(state.root);
    return ELISA_USER_DATA_OK;
}

int32_t remove_single(const std::filesystem::path& path) {
    std::error_code error;
    const bool removed = std::filesystem::remove(path, error);
    if (error) return ELISA_USER_DATA_IO_FAILURE;
    return removed ? ELISA_USER_DATA_OK : ELISA_USER_DATA_NOT_FOUND;
}

// A removed record must not resurface from its recovery copy.
int32_t remove_file(const std::filesystem::path& path) {
    const int32_t primary = remove_single(path);
    const int32_t backup = remove_single(backup_of(path));
    if (primary == ELISA_USER_DATA_IO_FAILURE || backup == ELISA_USER_DATA_IO_FAILURE) {
        return ELISA_USER_DATA_IO_FAILURE;
    }
    return (primary == ELISA_USER_DATA_OK || backup == ELISA_USER_DATA_OK)
        ? ELISA_USER_DATA_OK : ELISA_USER_DATA_NOT_FOUND;
}

} // namespace

extern "C" uint32_t elisa_user_data_abi_version() {
    return ABI_VERSION;
}

// Removes staging files a crash left behind. Recent ones are kept, since
// another process of the same application may still be writing them.
void sweep_orphans(const std::filesystem::path& root) {
    constexpr auto MAX_AGE = std::chrono::hours(1);
    std::error_code error;
    const auto now = std::filesystem::file_time_type::clock::now();
    for (std::filesystem::directory_iterator it(root, error), end; !error && it != end; it.increment(error)) {
        const std::string name = it->path().filename().string();
        if (name.find(".tmp-") == std::string::npos) continue;
        std::error_code entry_error;
        if (!it->is_regular_file(entry_error) || entry_error) continue;
        const auto written = std::filesystem::last_write_time(it->path(), entry_error);
        if (entry_error || now - written < MAX_AGE) continue;
        std::filesystem::remove(it->path(), entry_error);
    }
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

    sweep_orphans(root);
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
    return write_atomic_file(state, key, target, bytes);
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
    const int32_t loaded = read_recovering(target, bytes, MAX_FILE_BYTES, 40);
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
    return remove_file(state.root / (std::string(key) + ".save"));
}

extern "C" int32_t elisa_user_data_v1_write_payload(const char* key, int64_t version,
        const uint8_t* payload, uint32_t length) {
    if (version <= 0 || payload == nullptr || length == 0 ||
        length > ELISA_USER_DATA_MAX_PAYLOAD_BYTES) return ELISA_USER_DATA_INVALID_ARGUMENT;
    ServiceState& state = service();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!state.initialized) return ELISA_USER_DATA_NOT_INITIALIZED;
    if (!valid_component(key, 48, false)) return ELISA_USER_DATA_INVALID_KEY;

    std::vector<uint8_t> bytes;
    bytes.reserve(PAYLOAD_HEADER_BYTES + length + 8);
    append_u32(bytes, PAYLOAD_MAGIC);
    append_u32(bytes, PAYLOAD_FILE_VERSION);
    append_u64(bytes, static_cast<uint64_t>(version));
    append_u32(bytes, length);
    append_u32(bytes, 0);
    bytes.insert(bytes.end(), payload, payload + length);
    append_u64(bytes, checksum(bytes.data(), bytes.size()));
    const std::filesystem::path target = state.root / (std::string(key) + ".data");
    return write_atomic_file(state, key, target, bytes);
}

extern "C" int32_t elisa_user_data_v1_read_payload(const char* key, int64_t* version,
        uint8_t* payload, uint32_t capacity, uint32_t* length) {
    if (version == nullptr || payload == nullptr || length == nullptr || capacity == 0) {
        return ELISA_USER_DATA_INVALID_ARGUMENT;
    }
    ServiceState& state = service();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!state.initialized) return ELISA_USER_DATA_NOT_INITIALIZED;
    if (!valid_component(key, 48, false)) return ELISA_USER_DATA_INVALID_KEY;

    std::vector<uint8_t> bytes;
    const std::filesystem::path target = state.root / (std::string(key) + ".data");
    const int32_t loaded = read_recovering(target, bytes, MAX_PAYLOAD_FILE_BYTES,
        PAYLOAD_HEADER_BYTES + 1 + 8);
    if (loaded != ELISA_USER_DATA_OK) return loaded;

    size_t offset = 0;
    uint32_t magic = 0;
    uint32_t file_version = 0;
    uint64_t record_version = 0;
    uint32_t payload_length = 0;
    uint32_t reserved = 0;
    if (!read_u32(bytes, offset, magic) || !read_u32(bytes, offset, file_version) ||
        !read_u64(bytes, offset, record_version) || !read_u32(bytes, offset, payload_length) ||
        !read_u32(bytes, offset, reserved)) return ELISA_USER_DATA_CORRUPT;
    if (magic != PAYLOAD_MAGIC) return ELISA_USER_DATA_CORRUPT;
    if (file_version != PAYLOAD_FILE_VERSION) return ELISA_USER_DATA_WRONG_VERSION;
    if (record_version == 0 || record_version > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) ||
        payload_length == 0 || payload_length > ELISA_USER_DATA_MAX_PAYLOAD_BYTES || reserved != 0) {
        return ELISA_USER_DATA_CORRUPT;
    }
    if (payload_length > capacity) return ELISA_USER_DATA_CAPACITY;
    if (bytes.size() != PAYLOAD_HEADER_BYTES + static_cast<size_t>(payload_length) + 8) {
        return ELISA_USER_DATA_CORRUPT;
    }
    uint64_t stored_checksum = 0;
    const size_t checksum_offset = bytes.size() - 8;
    size_t checksum_reader = checksum_offset;
    if (!read_u64(bytes, checksum_reader, stored_checksum) ||
        checksum(bytes.data(), checksum_offset) != stored_checksum) return ELISA_USER_DATA_CORRUPT;

    const uint8_t* staged = bytes.data() + offset;
    std::memcpy(version, &record_version, sizeof(record_version));
    *length = payload_length;
    std::memset(payload, 0, capacity);
    std::memcpy(payload, staged, payload_length);
    return ELISA_USER_DATA_OK;
}

extern "C" int32_t elisa_user_data_v1_remove_payload(const char* key) {
    ServiceState& state = service();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!state.initialized) return ELISA_USER_DATA_NOT_INITIALIZED;
    if (!valid_component(key, 48, false)) return ELISA_USER_DATA_INVALID_KEY;
    return remove_file(state.root / (std::string(key) + ".data"));
}
