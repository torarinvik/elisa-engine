#include "../native/user_data_abi.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>

namespace {

bool expect(bool condition, const char* message) {
    if (!condition) std::cerr << "user-data service test failed: " << message << '\n';
    return condition;
}

bool set_user_data_root(const std::string& value) {
#if defined(_WIN32)
    return _putenv_s("ELISA_USER_DATA_DIR", value.c_str()) == 0;
#else
    return setenv("ELISA_USER_DATA_DIR", value.c_str(), 1) == 0;
#endif
}

} // namespace

int main() {
    using namespace std::chrono;
    const auto unique = duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
    const std::filesystem::path scratch = std::filesystem::temp_directory_path() /
        ("elisa-user-data-test-" + std::to_string(unique));
    std::error_code error;
    std::filesystem::create_directories(scratch, error);
    if (!expect(!error, "create temporary directory")) return 1;

    const std::filesystem::path base = scratch / "user-data";
    if (!expect(set_user_data_root(base.string()), "set isolated user-data root")) return 2;
    if (!expect(elisa_user_data_abi_version() == 1, "ABI version")) return 3;

    int64_t initial_fields[3] = {-17, std::numeric_limits<int64_t>::min(), 42};
    int64_t loaded_version = 91;
    int64_t loaded_fields[ELISA_USER_DATA_MAX_FIELDS] = {};
    for (int64_t& field : loaded_fields) field = 91;
    uint32_t loaded_count = 91;
    const uint8_t payload_bytes[6] = {0, 1, 2, 127, 128, 255};
    uint8_t loaded_payload[8] = {91, 91, 91, 91, 91, 91, 91, 91};
    uint32_t loaded_length = 91;
    if (!expect(elisa_user_data_v1_write_blob("settings", 4, initial_fields, 3) ==
            ELISA_USER_DATA_NOT_INITIALIZED, "write before initialization")) return 4;
    if (!expect(elisa_user_data_v1_read_blob("settings", &loaded_version,
            loaded_fields, ELISA_USER_DATA_MAX_FIELDS, &loaded_count) == ELISA_USER_DATA_NOT_INITIALIZED,
            "read before initialization")) return 5;
    if (!expect(elisa_user_data_v1_write_payload("bytes", 2, payload_bytes, 6) ==
            ELISA_USER_DATA_NOT_INITIALIZED, "write payload before initialization")) return 37;
    if (!expect(elisa_user_data_v1_initialize("../bad") == ELISA_USER_DATA_INVALID_KEY,
            "reject traversal application id")) return 6;
    if (!expect(elisa_user_data_v1_initialize("test.wall-game") == ELISA_USER_DATA_OK,
            "initialize service")) return 7;

    const std::filesystem::path expected_root = base / "test.wall-game";
    if (!expect(std::filesystem::path(elisa_user_data_v1_directory()) == expected_root,
            "reported directory")) return 8;
    if (!expect(elisa_user_data_v1_write_blob("../outside", 4, initial_fields, 3) ==
            ELISA_USER_DATA_INVALID_KEY, "reject traversal key")) return 9;
    if (!expect(elisa_user_data_v1_read_blob("missing", &loaded_version,
            loaded_fields, ELISA_USER_DATA_MAX_FIELDS, &loaded_count) == ELISA_USER_DATA_NOT_FOUND,
            "missing file status")) return 10;
    if (!expect(loaded_version == 91 && loaded_count == 91 && loaded_fields[0] == 91,
            "failed read preserves outputs")) return 11;
    if (!expect(elisa_user_data_v1_write_blob("settings", 0, initial_fields, 3) ==
            ELISA_USER_DATA_INVALID_ARGUMENT, "reject invalid record version")) return 12;
    if (!expect(elisa_user_data_v1_write_blob("settings", 4, initial_fields, 3) ==
            ELISA_USER_DATA_OK, "write settings")) return 13;
    if (!expect(elisa_user_data_v1_write_payload("../outside", 2, payload_bytes, 6) ==
            ELISA_USER_DATA_INVALID_KEY, "reject payload traversal key")) return 38;
    if (!expect(elisa_user_data_v1_write_payload("bytes", 0, payload_bytes, 6) ==
            ELISA_USER_DATA_INVALID_ARGUMENT, "reject payload version zero")) return 39;
    if (!expect(elisa_user_data_v1_write_payload("bytes", 2, payload_bytes, 0) ==
            ELISA_USER_DATA_INVALID_ARGUMENT, "reject empty payload")) return 40;
    if (!expect(elisa_user_data_v1_write_payload("bytes", 2, payload_bytes,
            ELISA_USER_DATA_MAX_PAYLOAD_BYTES + 1) == ELISA_USER_DATA_INVALID_ARGUMENT,
            "reject oversized payload before reading memory")) return 41;
    if (!expect(elisa_user_data_v1_read_payload("missing", &loaded_version,
            loaded_payload, sizeof(loaded_payload), &loaded_length) == ELISA_USER_DATA_NOT_FOUND,
            "report missing payload")) return 42;
    if (!expect(loaded_version == 91 && loaded_length == 91 && loaded_payload[0] == 91,
            "missing payload preserves output buffers")) return 43;
    if (!expect(elisa_user_data_v1_write_payload("bytes", 2, payload_bytes, 6) ==
            ELISA_USER_DATA_OK, "write bounded payload")) return 44;
    loaded_version = 91;
    loaded_length = 91;
    for (uint8_t& value : loaded_payload) value = 91;
    if (!expect(elisa_user_data_v1_read_payload("bytes", &loaded_version,
            loaded_payload, 4, &loaded_length) == ELISA_USER_DATA_CAPACITY,
            "reject undersized payload buffer")) return 45;
    if (!expect(loaded_version == 91 && loaded_length == 91 && loaded_payload[0] == 91,
            "capacity failure preserves payload outputs")) return 46;
    if (!expect(elisa_user_data_v1_read_payload("bytes", &loaded_version,
            loaded_payload, sizeof(loaded_payload), &loaded_length) == ELISA_USER_DATA_OK,
            "read bounded payload")) return 47;
    if (!expect(loaded_version == 2 && loaded_length == 6 &&
            std::equal(std::begin(payload_bytes), std::end(payload_bytes), std::begin(loaded_payload)) &&
            loaded_payload[6] == 0 && loaded_payload[7] == 0,
            "round-trip arbitrary bytes and clear unused output")) return 48;
    const uint8_t replacement_payload[3] = {255, 127, 0};
    if (!expect(elisa_user_data_v1_write_payload("bytes", 3, replacement_payload, 3) ==
            ELISA_USER_DATA_OK, "replace payload record")) return 54;
    if (!expect(elisa_user_data_v1_read_payload("bytes", &loaded_version,
            loaded_payload, sizeof(loaded_payload), &loaded_length) == ELISA_USER_DATA_OK &&
            loaded_version == 3 && loaded_length == 3 &&
            std::equal(std::begin(replacement_payload), std::end(replacement_payload),
                std::begin(loaded_payload)), "read replacement payload")) return 55;

    loaded_version = 0;
    loaded_count = 0;
    for (int64_t& field : loaded_fields) field = 0;
    if (!expect(elisa_user_data_v1_read_blob("settings", &loaded_version,
            loaded_fields, 2, &loaded_count) == ELISA_USER_DATA_CAPACITY,
            "reject undersized read buffer")) return 14;
    if (!expect(loaded_version == 0 && loaded_count == 0 && loaded_fields[0] == 0,
            "capacity failure preserves outputs")) return 15;
    if (!expect(elisa_user_data_v1_read_blob("settings", &loaded_version,
            loaded_fields, ELISA_USER_DATA_MAX_FIELDS, &loaded_count) == ELISA_USER_DATA_OK,
            "read settings")) return 16;
    if (!expect(loaded_version == 4 && loaded_count == 3 &&
            loaded_fields[0] == initial_fields[0] &&
            loaded_fields[1] == initial_fields[1] &&
            loaded_fields[2] == initial_fields[2], "round-trip signed fields")) return 17;

    int64_t maximum_fields[ELISA_USER_DATA_MAX_FIELDS] = {};
    for (uint32_t index = 0; index < ELISA_USER_DATA_MAX_FIELDS; ++index) {
        maximum_fields[index] = static_cast<int64_t>(index * 17) - 31;
    }
    if (!expect(elisa_user_data_v1_write_blob("maximum", 5, maximum_fields,
            ELISA_USER_DATA_MAX_FIELDS) == ELISA_USER_DATA_OK,
            "write maximum field record")) return 34;
    loaded_version = 0;
    loaded_count = 0;
    for (int64_t& field : loaded_fields) field = 0;
    if (!expect(elisa_user_data_v1_read_blob("maximum", &loaded_version,
            loaded_fields, ELISA_USER_DATA_MAX_FIELDS, &loaded_count) == ELISA_USER_DATA_OK &&
            loaded_version == 5 && loaded_count == ELISA_USER_DATA_MAX_FIELDS,
            "read maximum field record")) return 35;
    for (uint32_t index = 0; index < ELISA_USER_DATA_MAX_FIELDS; ++index) {
        if (!expect(loaded_fields[index] == maximum_fields[index],
                "maximum field round-trip")) return 36;
    }

    const int64_t replacement_fields[1] = {777};
    if (!expect(elisa_user_data_v1_write_blob("settings", 9, replacement_fields, 1) ==
            ELISA_USER_DATA_OK, "replace settings atomically")) return 18;
    if (!expect(elisa_user_data_v1_read_blob("settings", &loaded_version,
            loaded_fields, ELISA_USER_DATA_MAX_FIELDS, &loaded_count) == ELISA_USER_DATA_OK &&
            loaded_version == 9 && loaded_count == 1 && loaded_fields[0] == 777,
            "read replacement")) return 19;

    if (!expect(elisa_user_data_v1_write_blob("broken", 1, replacement_fields, 1) ==
            ELISA_USER_DATA_OK, "write corruption fixture")) return 20;
    {
        std::fstream file(expected_root / "broken.save", std::ios::binary | std::ios::in | std::ios::out);
        char damaged = '\0';
        file.seekp(0);
        file.write(&damaged, 1);
        if (!expect(static_cast<bool>(file), "corrupt fixture")) return 21;
    }
    if (!expect(elisa_user_data_v1_read_blob("broken", &loaded_version,
            loaded_fields, ELISA_USER_DATA_MAX_FIELDS, &loaded_count) == ELISA_USER_DATA_CORRUPT,
            "reject corrupt file")) return 22;

    if (!expect(elisa_user_data_v1_write_payload("broken-bytes", 1, payload_bytes, 6) ==
            ELISA_USER_DATA_OK, "write payload corruption fixture")) return 49;
    {
        std::fstream file(expected_root / "broken-bytes.data",
            std::ios::binary | std::ios::in | std::ios::out);
        const char damaged = 0x7f;
        file.seekp(24);
        file.write(&damaged, 1);
        if (!expect(static_cast<bool>(file), "corrupt payload fixture")) return 50;
    }
    if (!expect(elisa_user_data_v1_read_payload("broken-bytes", &loaded_version,
            loaded_payload, sizeof(loaded_payload), &loaded_length) == ELISA_USER_DATA_CORRUPT,
            "reject payload checksum mismatch")) return 51;
    if (!expect(elisa_user_data_v1_write_payload("truncated-bytes", 1, payload_bytes, 6) ==
            ELISA_USER_DATA_OK, "write truncated payload fixture")) return 56;
    std::filesystem::resize_file(expected_root / "truncated-bytes.data", 30, error);
    if (!expect(!error, "truncate payload fixture")) return 57;
    if (!expect(elisa_user_data_v1_read_payload("truncated-bytes", &loaded_version,
            loaded_payload, sizeof(loaded_payload), &loaded_length) == ELISA_USER_DATA_CORRUPT,
            "reject truncated payload file")) return 58;
    if (!expect(elisa_user_data_v1_write_payload("future-bytes", 1, payload_bytes, 6) ==
            ELISA_USER_DATA_OK, "write future payload fixture")) return 59;
    {
        std::fstream file(expected_root / "future-bytes.data",
            std::ios::binary | std::ios::in | std::ios::out);
        const char future_version = 2;
        file.seekp(4);
        file.write(&future_version, 1);
        if (!expect(static_cast<bool>(file), "mark future payload format")) return 60;
    }
    if (!expect(elisa_user_data_v1_read_payload("future-bytes", &loaded_version,
            loaded_payload, sizeof(loaded_payload), &loaded_length) == ELISA_USER_DATA_WRONG_VERSION,
            "report unsupported payload envelope version")) return 61;
    if (!expect(elisa_user_data_v1_remove_payload("absent") == ELISA_USER_DATA_NOT_FOUND,
            "remove absent payload")) return 52;
    if (!expect(elisa_user_data_v1_remove_payload("bytes") == ELISA_USER_DATA_OK,
            "remove payload")) return 53;

    if (!expect(elisa_user_data_v1_write_blob("bad-count", 1, replacement_fields, 1) ==
            ELISA_USER_DATA_OK, "write invalid-count fixture")) return 31;
    {
        std::fstream file(expected_root / "bad-count.save", std::ios::binary | std::ios::in | std::ios::out);
        const char invalid_count = 9;
        file.seekp(16);
        file.write(&invalid_count, 1);
        if (!expect(static_cast<bool>(file), "mark invalid field count")) return 32;
    }
    if (!expect(elisa_user_data_v1_read_blob("bad-count", &loaded_version,
            loaded_fields, ELISA_USER_DATA_MAX_FIELDS, &loaded_count) == ELISA_USER_DATA_CORRUPT,
            "reject invalid field count")) return 33;

    if (!expect(elisa_user_data_v1_write_blob("future", 1, replacement_fields, 1) ==
            ELISA_USER_DATA_OK, "write future-version fixture")) return 23;
    {
        std::fstream file(expected_root / "future.save", std::ios::binary | std::ios::in | std::ios::out);
        const char future_version = 2;
        file.seekp(4);
        file.write(&future_version, 1);
        if (!expect(static_cast<bool>(file), "mark future file version")) return 24;
    }
    if (!expect(elisa_user_data_v1_read_blob("future", &loaded_version,
            loaded_fields, ELISA_USER_DATA_MAX_FIELDS, &loaded_count) == ELISA_USER_DATA_WRONG_VERSION,
            "report unsupported format version")) return 25;
    // Crash recovery: the previous intact record survives as a backup, and a
    // damaged or missing primary falls back to it.
    const int64_t first_generation[1] = {111};
    const int64_t second_generation[1] = {222};
    if (!expect(elisa_user_data_v1_write_blob("recover", 1, first_generation, 1) == ELISA_USER_DATA_OK &&
            elisa_user_data_v1_write_blob("recover", 2, second_generation, 1) == ELISA_USER_DATA_OK,
            "write two generations")) return 70;
    if (!expect(elisa_user_data_v1_read_blob("recover", &loaded_version,
            loaded_fields, ELISA_USER_DATA_MAX_FIELDS, &loaded_count) == ELISA_USER_DATA_OK &&
            loaded_version == 2 && loaded_fields[0] == 222, "read newest generation")) return 71;
    {
        std::fstream file(expected_root / "recover.save", std::ios::binary | std::ios::in | std::ios::out);
        const char damaged = 0x55;
        file.seekp(30);
        file.write(&damaged, 1);
        if (!expect(static_cast<bool>(file), "damage primary")) return 72;
    }
    if (!expect(elisa_user_data_v1_read_blob("recover", &loaded_version,
            loaded_fields, ELISA_USER_DATA_MAX_FIELDS, &loaded_count) == ELISA_USER_DATA_OK &&
            loaded_version == 1 && loaded_fields[0] == 111, "recover last good generation")) return 73;
    std::filesystem::remove(expected_root / "recover.save", error);
    if (!expect(elisa_user_data_v1_read_blob("recover", &loaded_version,
            loaded_fields, ELISA_USER_DATA_MAX_FIELDS, &loaded_count) == ELISA_USER_DATA_OK &&
            loaded_fields[0] == 111, "recover when the primary is missing")) return 74;
    {
        std::fstream file(expected_root / "recover.save.bak", std::ios::binary | std::ios::in | std::ios::out);
        const char damaged = 0x55;
        file.seekp(30);
        file.write(&damaged, 1);
    }
    if (!expect(elisa_user_data_v1_read_blob("recover", &loaded_version,
            loaded_fields, ELISA_USER_DATA_MAX_FIELDS, &loaded_count) == ELISA_USER_DATA_NOT_FOUND,
            "a damaged backup is never used")) return 75;
    if (!expect(elisa_user_data_v1_write_blob("recover", 3, second_generation, 1) == ELISA_USER_DATA_OK &&
            elisa_user_data_v1_remove_blob("recover") == ELISA_USER_DATA_OK &&
            elisa_user_data_v1_read_blob("recover", &loaded_version,
                loaded_fields, ELISA_USER_DATA_MAX_FIELDS, &loaded_count) == ELISA_USER_DATA_NOT_FOUND &&
            !std::filesystem::exists(expected_root / "recover.save.bak", error),
            "remove deletes the recovery copy")) return 76;
    if (!expect(elisa_user_data_v1_write_payload("recover-bytes", 1, payload_bytes, 6) == ELISA_USER_DATA_OK &&
            elisa_user_data_v1_write_payload("recover-bytes", 2, payload_bytes, 5) == ELISA_USER_DATA_OK,
            "write two payload generations")) return 77;
    std::filesystem::resize_file(expected_root / "recover-bytes.data", 30, error);
    if (!expect(!error && elisa_user_data_v1_read_payload("recover-bytes", &loaded_version,
            loaded_payload, sizeof(loaded_payload), &loaded_length) == ELISA_USER_DATA_OK &&
            loaded_version == 1 && loaded_length == 6, "recover a truncated payload")) return 78;
    if (!expect(elisa_user_data_v1_remove_blob("absent") == ELISA_USER_DATA_NOT_FOUND,
            "remove absent file")) return 26;
    if (!expect(elisa_user_data_v1_remove_blob("settings") == ELISA_USER_DATA_OK,
            "remove settings")) return 27;

    const std::filesystem::path blocked = scratch / "not-a-directory";
    {
        std::ofstream file(blocked);
        file << "file";
    }
    if (!expect(set_user_data_root(blocked.string()), "set failing root")) return 28;
    if (!expect(elisa_user_data_v1_initialize("another.app") == ELISA_USER_DATA_IO_FAILURE,
            "report unusable storage root")) return 29;
    if (!expect(std::filesystem::path(elisa_user_data_v1_directory()) == expected_root,
            "failed initialize preserves prior service state")) return 30;

    std::filesystem::remove_all(scratch, error);
    std::cout << "User-data native service tests passed.\n";
    return 0;
}
