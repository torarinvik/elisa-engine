#pragma once

#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include "shader_manifest_validation.h"

namespace elisa::shader {

constexpr size_t MAX_ROOT_LENGTH = 4096;
inline bool manifest_is_valid(const std::filesystem::path& root, const char* configured) {
    if (configured == nullptr || configured[0] == '\0') return true;
    std::error_code error;
    const std::filesystem::path path = std::filesystem::path(configured).is_absolute()
        ? std::filesystem::path(configured) : root / configured;
    const std::filesystem::path canonical_root = std::filesystem::weakly_canonical(root, error);
    if (error) return false;
    const std::filesystem::path canonical_manifest = std::filesystem::weakly_canonical(path, error);
    if (error || canonical_manifest.parent_path() != canonical_root ||
        !std::filesystem::is_regular_file(canonical_manifest, error) || error) {
        return false;
    }
    const uintmax_t manifest_size = std::filesystem::file_size(canonical_manifest, error);
    if (error || manifest_size > MAX_MANIFEST_BYTES) return false;
    std::ifstream stream(canonical_manifest, std::ios::binary);
    if (!stream) return false;
    std::string content(static_cast<size_t>(manifest_size), '\0');
    if (!content.empty()) stream.read(content.data(), static_cast<std::streamsize>(content.size()));
    if (!stream || stream.gcount() != static_cast<std::streamsize>(content.size())) return false;
    return verify_shader_manifest(canonical_root, content);
}

inline int root_status(const char* configured, const char* manifest) {
    if (configured == nullptr || configured[0] == '\0') {
        return manifest == nullptr || manifest[0] == '\0' ? 0 : -8;
    }
    if (std::strlen(configured) > MAX_ROOT_LENGTH) return -7;
    const std::filesystem::path root(configured);
    std::error_code error;
    if (!std::filesystem::is_directory(root, error) || error) return -7;
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
    if (!std::filesystem::is_directory(platform_root, error) || error) return -7;
    for (std::filesystem::directory_iterator iterator(platform_root, error), end;
        iterator != end && !error; iterator.increment(error)) {
        if (iterator->is_regular_file(error) && !error &&
            iterator->path().extension() == binary_suffix) {
            return manifest_is_valid(root, manifest) ? 0 : -8;
        }
    }
    return -7;
}

inline bool root_is_valid(const char* configured, const char* manifest = nullptr) {
    return root_status(configured, manifest) == 0;
}

inline bool manifest_status_is_invalid(const char* configured, const char* manifest) {
    return root_status(configured, manifest) == -8;
}

} // namespace elisa::shader
