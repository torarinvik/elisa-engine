#pragma once

// A cooked ELPK bundle names the bundles it needs in its manifest section, as
// project-relative paths. Before a mesh or texture is read from a bundle, its
// whole dependency closure must exist under the project root, with valid
// manifests and no cycle. Only presence is checked; dependencies aren't loaded.
#include "package_manifest.h"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace elisa::assets {

inline constexpr size_t MAX_BUNDLE_DEPENDENCIES = 16;

// Project asset paths resolve under ELISA_PROJECT_ROOT, or under the working
// directory when that variable is unset.
inline bool project_asset_root(std::filesystem::path& root) {
    std::error_code error;
    const char* configured = std::getenv("ELISA_PROJECT_ROOT");
    root = configured != nullptr && configured[0] != '\0'
        ? std::filesystem::canonical(std::filesystem::u8path(configured), error)
        : std::filesystem::canonical(std::filesystem::current_path(error), error);
    if (error || !std::filesystem::is_directory(root, error) || error) return false;
    return true;
}

inline bool is_elpk_bundle(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::array<char, 4> magic{};
    input.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    return input.gcount() == static_cast<std::streamsize>(magic.size()) &&
        magic == std::array<char, 4>{'E', 'L', 'P', 'K'};
}

// `bundle` is a canonical path returned by resolve_project_asset_path. Loose
// cooked packages have no manifest and pass unchanged.
inline bool verify_bundle_dependencies(const std::filesystem::path& bundle, std::string& error) {
    if (!is_elpk_bundle(bundle)) return true;
    std::filesystem::path root;
    if (!project_asset_root(root)) {
        error = "project asset root is unavailable";
        return false;
    }
    std::vector<std::string> order;
    return probe::package_dependency_order(root, {}, bundle.lexically_relative(root).generic_string(),
        MAX_BUNDLE_DEPENDENCIES, order, error);
}

} // namespace elisa::assets
