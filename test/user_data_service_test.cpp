#include "../native/user_data_abi.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
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
    int64_t loaded_fields[8] = {91, 91, 91, 91, 91, 91, 91, 91};
    uint32_t loaded_count = 91;
    if (!expect(elisa_user_data_v1_write_blob("settings", 4, initial_fields, 3) ==
            ELISA_USER_DATA_NOT_INITIALIZED, "write before initialization")) return 4;
    if (!expect(elisa_user_data_v1_read_blob("settings", &loaded_version,
            loaded_fields, 8, &loaded_count) == ELISA_USER_DATA_NOT_INITIALIZED,
            "read before initialization")) return 5;
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
            loaded_fields, 8, &loaded_count) == ELISA_USER_DATA_NOT_FOUND,
            "missing file status")) return 10;
    if (!expect(loaded_version == 91 && loaded_count == 91 && loaded_fields[0] == 91,
            "failed read preserves outputs")) return 11;
    if (!expect(elisa_user_data_v1_write_blob("settings", 0, initial_fields, 3) ==
            ELISA_USER_DATA_INVALID_ARGUMENT, "reject invalid record version")) return 12;
    if (!expect(elisa_user_data_v1_write_blob("settings", 4, initial_fields, 3) ==
            ELISA_USER_DATA_OK, "write settings")) return 13;

    loaded_version = 0;
    loaded_count = 0;
    for (int64_t& field : loaded_fields) field = 0;
    if (!expect(elisa_user_data_v1_read_blob("settings", &loaded_version,
            loaded_fields, 2, &loaded_count) == ELISA_USER_DATA_CAPACITY,
            "reject undersized read buffer")) return 14;
    if (!expect(loaded_version == 0 && loaded_count == 0 && loaded_fields[0] == 0,
            "capacity failure preserves outputs")) return 15;
    if (!expect(elisa_user_data_v1_read_blob("settings", &loaded_version,
            loaded_fields, 8, &loaded_count) == ELISA_USER_DATA_OK,
            "read settings")) return 16;
    if (!expect(loaded_version == 4 && loaded_count == 3 &&
            loaded_fields[0] == initial_fields[0] &&
            loaded_fields[1] == initial_fields[1] &&
            loaded_fields[2] == initial_fields[2], "round-trip signed fields")) return 17;

    const int64_t replacement_fields[1] = {777};
    if (!expect(elisa_user_data_v1_write_blob("settings", 9, replacement_fields, 1) ==
            ELISA_USER_DATA_OK, "replace settings atomically")) return 18;
    if (!expect(elisa_user_data_v1_read_blob("settings", &loaded_version,
            loaded_fields, 8, &loaded_count) == ELISA_USER_DATA_OK &&
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
            loaded_fields, 8, &loaded_count) == ELISA_USER_DATA_CORRUPT,
            "reject corrupt file")) return 22;

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
            loaded_fields, 8, &loaded_count) == ELISA_USER_DATA_CORRUPT,
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
            loaded_fields, 8, &loaded_count) == ELISA_USER_DATA_WRONG_VERSION,
            "report unsupported format version")) return 25;
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
