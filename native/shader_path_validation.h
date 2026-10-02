#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include "shader_manifest_validation.h"

namespace elisa::shader {

constexpr size_t MAX_ROOT_LENGTH = 4096;
inline const char* current_backend() {
#if defined(__APPLE__)
    return "metal";
#elif defined(_WIN32)
    return "hlsl6";
#else
    return "spirv";
#endif
}
// Returns an empty string when the configured manifest (if any) verifies,
// otherwise a diagnostic naming the manifest or shader file that failed.
inline std::string manifest_failure_at(const std::filesystem::path& root, const char* configured) {
    if (configured == nullptr || configured[0] == '\0') return {};
    std::error_code error;
    const std::filesystem::path path = std::filesystem::path(configured).is_absolute()
        ? std::filesystem::path(configured) : root / configured;
    const std::filesystem::path canonical_root = std::filesystem::weakly_canonical(root, error);
    if (error) return "shader root is unreadable: " + root.string();
    const std::filesystem::path canonical_manifest = std::filesystem::weakly_canonical(path, error);
    if (error || canonical_manifest.parent_path() != canonical_root ||
        !std::filesystem::is_regular_file(canonical_manifest, error) || error) {
        return "manifest is missing or outside the shader root: " + path.string();
    }
    const uintmax_t manifest_size = std::filesystem::file_size(canonical_manifest, error);
    if (error || manifest_size > MAX_MANIFEST_BYTES) return "manifest is unreadable or too large: " + path.string();
    std::ifstream stream(canonical_manifest, std::ios::binary);
    if (!stream) return "manifest is unreadable: " + path.string();
    std::string content(static_cast<size_t>(manifest_size), '\0');
    if (!content.empty()) stream.read(content.data(), static_cast<std::streamsize>(content.size()));
    if (!stream || stream.gcount() != static_cast<std::streamsize>(content.size())) {
        return "manifest is unreadable: " + path.string();
    }
    return shader_manifest_failure(canonical_root, content, current_backend());
}

inline bool manifest_is_valid(const std::filesystem::path& root, const char* configured) {
    return manifest_failure_at(root, configured).empty();
}

inline bool manifest_content_digest(const char* configured_root, const char* configured_manifest,
        std::string& digest) {
    if (configured_root == nullptr || configured_root[0] == '\0' ||
        configured_manifest == nullptr || configured_manifest[0] == '\0') return false;
    std::error_code error;
    const std::filesystem::path root = std::filesystem::weakly_canonical(configured_root, error);
    if (error) return false;
    const std::filesystem::path configured_path(configured_manifest);
    const std::filesystem::path path = std::filesystem::weakly_canonical(
        configured_path.is_absolute() ? configured_path : root / configured_path, error);
    if (error || path.parent_path() != root ||
        !std::filesystem::is_regular_file(path, error) || error) return false;
    const uintmax_t size = std::filesystem::file_size(path, error);
    if (error || size > MAX_MANIFEST_BYTES) return false;
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return false;
    std::string content(static_cast<size_t>(size), '\0');
    if (!content.empty()) stream.read(content.data(), static_cast<std::streamsize>(content.size()));
    if (!stream || stream.gcount() != static_cast<std::streamsize>(content.size())) return false;
    elisa::assets::Sha256 hash;
    hash.update(reinterpret_cast<const uint8_t*>(content.data()), content.size());
    digest = hash.finish();
    return true;
}

// The archive key binds a captured pipeline archive to everything that changes
// the pipelines it holds: the exact verified manifest bytes (so any shader
// change, addition or removal), the active shader backend, and the engine
// build identity (so a new engine binary never reuses an older capture).
inline std::string pipeline_archive_key(const std::string& manifest_digest,
    const std::string& backend, int64_t engine_build) {
    const std::string material = "elisa-pipeline-archive-v1\n" + manifest_digest + "\n" +
        backend + "\n" + std::to_string(engine_build);
    elisa::assets::Sha256 hash;
    hash.update(reinterpret_cast<const uint8_t*>(material.data()), material.size());
    return hash.finish();
}

inline void configure_metal_pipeline_archive_shader_key(const char* shader_root, const char* manifest_path,
    int64_t engine_build) {
#if defined(__APPLE__)
    std::string digest;
    if (manifest_content_digest(shader_root, manifest_path, digest)) {
        const std::string key = pipeline_archive_key(digest, "metal", engine_build);
        setenv("WICKED_METAL_PIPELINE_ARCHIVE_SHADER_KEY", key.c_str(), 1);
    } else {
        unsetenv("WICKED_METAL_PIPELINE_ARCHIVE_SHADER_KEY");
    }
#else
    (void)shader_root;
    (void)manifest_path;
    (void)engine_build;
#endif
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

inline std::string manifest_failure(const char* configured, const char* manifest) {
    if (configured == nullptr || configured[0] == '\0') return "manifest configured without a shader root";
    return manifest_failure_at(std::filesystem::path(configured), manifest);
}

} // namespace elisa::shader
