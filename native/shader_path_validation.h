#pragma once

#include <cstddef>
#include <cstring>
#include <filesystem>

namespace elisa::shader {

constexpr size_t MAX_ROOT_LENGTH = 4096;

inline bool root_is_valid(const char* configured) {
    if (configured == nullptr || configured[0] == '\0') return true;
    if (std::strlen(configured) > MAX_ROOT_LENGTH) return false;
    const std::filesystem::path root(configured);
    std::error_code error;
    if (!std::filesystem::is_directory(root, error) || error) return false;
#if defined(__APPLE__)
    const std::filesystem::path platform_root = root / "metal";
    const char* binary_suffix = ".cso";
#elif defined(_WIN32)
    const std::filesystem::path platform_root = root / "hlsl6";
    const char* binary_suffix = ".cso";
#else
    const std::filesystem::path platform_root = root / "spirv";
    const char* binary_suffix = ".spv";
#endif
    if (!std::filesystem::is_directory(platform_root, error) || error) return false;
    for (std::filesystem::directory_iterator iterator(platform_root, error), end;
        iterator != end && !error; iterator.increment(error)) {
        if (iterator->is_regular_file(error) && !error &&
            iterator->path().extension() == binary_suffix) return true;
    }
    return false;
}

} // namespace elisa::shader
