#pragma once

#include "package_load.h"
#include "probe_support.h"

#include <filesystem>
#include <fstream>

namespace probe {

inline bool probe_package_bounds(const std::string& valid_package) {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "elisa-package-probe";
    std::filesystem::create_directories(root);
    const std::filesystem::path duplicate = root / "duplicate.pkg";
    std::ofstream(duplicate) << "format=elisa-cooked-v2\nformat=other\n";
    const std::filesystem::path traversal = root / "traversal.pkg";
    std::ofstream(traversal) << "format=elisa-cooked-v2\nsource=../outside.pkg\n";
    const std::filesystem::path malformed = root / "malformed.pkg";
    std::ofstream(malformed) << "not-a-section\n";
    const bool result = check(load_cooked_package(valid_package).loaded, "bounded package load") &&
        check(!load_cooked_package(duplicate.string()).loaded, "duplicate package section rejected") &&
        check(!load_cooked_package(traversal.string()).loaded, "package traversal rejected") &&
        check(!load_cooked_package(malformed.string()).loaded, "malformed package rejected");
    std::filesystem::remove_all(root);
    return result;
}

} // namespace probe
