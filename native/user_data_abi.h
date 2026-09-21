#pragma once

#include <cstdint>

enum ElisaUserDataStatus : int32_t {
    ELISA_USER_DATA_OK = 0,
    ELISA_USER_DATA_NOT_INITIALIZED = 1,
    ELISA_USER_DATA_INVALID_KEY = 2,
    ELISA_USER_DATA_INVALID_ARGUMENT = 3,
    ELISA_USER_DATA_NOT_FOUND = 4,
    ELISA_USER_DATA_CAPACITY = 5,
    ELISA_USER_DATA_WRONG_VERSION = 6,
    ELISA_USER_DATA_CORRUPT = 7,
    ELISA_USER_DATA_IO_FAILURE = 8,
};

extern "C" {

uint32_t elisa_user_data_abi_version();
int32_t elisa_user_data_v1_initialize(const char* application_id);
const char* elisa_user_data_v1_directory();
int32_t elisa_user_data_v1_write_blob(const char* key, int64_t version,
    const int64_t* fields, uint32_t count);
int32_t elisa_user_data_v1_read_blob(const char* key, int64_t* version,
    int64_t* fields, uint32_t capacity, uint32_t* count);
int32_t elisa_user_data_v1_remove_blob(const char* key);

}
