#pragma once

// Bounded ELPK dependency manifests and deterministic prerequisite-first
// ordering. Manifest IO runs from VirtualFileService's worker pump.
#include "virtual_package.h"

#include <set>
#include <string>
#include <vector>

namespace probe {

struct BinaryPackageManifest {
    static constexpr size_t MAX_BYTES = 16 * 1024;
    static constexpr size_t MAX_STORED_BYTES = 2 * MAX_BYTES;
    static constexpr size_t MAX_DEPENDENCIES = 16;

    std::vector<std::string> dependencies;
    bool valid = false;
    std::string error;
};

inline bool parse_binary_package_manifest(const std::vector<uint8_t>& bytes,
    BinaryPackageManifest& manifest) {
    manifest = {};
    if (bytes.empty() || bytes.size() > BinaryPackageManifest::MAX_BYTES) {
        manifest.error = "package manifest size rejected";
        return false;
    }
    const std::string text(bytes.begin(), bytes.end());
    size_t offset = 0;
    bool header_seen = false;
    std::string previous_dependency;
    while (offset < text.size()) {
        const size_t newline = text.find('\n', offset);
        const size_t end = newline == std::string::npos ? text.size() : newline;
        const size_t length = end - offset;
        if (length == 0 || length > 255) {
            manifest.error = "package manifest line rejected";
            return false;
        }
        const std::string line = text.substr(offset, length);
        if (!header_seen) {
            if (line != "ELISA-PACKAGE-MANIFEST-1") {
                manifest.error = "package manifest header rejected";
                return false;
            }
            header_seen = true;
        } else {
            static const std::string PREFIX = "dependency=";
            if (line.find('\r') != std::string::npos || line.compare(0, PREFIX.size(), PREFIX) != 0 ||
                manifest.dependencies.size() >= BinaryPackageManifest::MAX_DEPENDENCIES) {
                manifest.error = "package manifest dependency rejected";
                return false;
            }
            const std::string dependency = line.substr(PREFIX.size());
            if (!safe_package_path(dependency) || (!previous_dependency.empty() && dependency <= previous_dependency)) {
                manifest.error = "package manifest dependency order rejected";
                return false;
            }
            manifest.dependencies.push_back(dependency);
            previous_dependency = dependency;
        }
        if (newline == std::string::npos) break;
        offset = newline + 1;
    }
    if (!header_seen) {
        manifest.error = "package manifest header rejected";
        return false;
    }
    manifest.valid = true;
    return true;
}

inline BinaryPackageManifest read_binary_package_manifest(const std::string& path) {
    BinaryPackageManifest manifest;
    const BinaryPackageIndex index = read_binary_package_index(path);
    if (!index.valid) {
        manifest.error = index.error.empty() ? "binary package index is invalid" : index.error;
        return manifest;
    }
    const auto section = std::find_if(index.sections.begin(), index.sections.end(),
        [](const BinaryPackageSection& value) { return value.name == "manifest"; });
    if (section == index.sections.end()) {
        manifest.valid = true;
        return manifest;
    }
    if (section->unpacked_size > BinaryPackageManifest::MAX_BYTES ||
        section->size > BinaryPackageManifest::MAX_STORED_BYTES) {
        manifest.error = "package manifest size rejected";
        return manifest;
    }
    std::vector<uint8_t> bytes;
    if (!read_binary_package_section(path, index, "manifest", bytes, manifest.error)) return manifest;
    parse_binary_package_manifest(bytes, manifest);
    return manifest;
}

inline bool append_package_dependency_order(const std::filesystem::path& base_root,
    const std::vector<std::filesystem::path>& override_roots, const std::string& package_name,
    size_t max_dependencies, std::set<std::string>& visiting, std::set<std::string>& visited,
    std::vector<std::string>& order, std::string& error) {
    if (visiting.count(package_name) != 0) {
        error = "package dependency cycle";
        return false;
    }
    if (visited.count(package_name) != 0) return true;
    if (visited.size() >= max_dependencies) {
        error = "package dependency count exceeded";
        return false;
    }
    const PackageResolution resolved = resolve_package_path(
        base_root, override_roots, package_name, 1);
    if (!resolved.found) {
        error = "package dependency is missing";
        return false;
    }
    const BinaryPackageManifest manifest = read_binary_package_manifest(resolved.path);
    if (!manifest.valid) {
        error = manifest.error;
        return false;
    }
    visiting.insert(package_name);
    for (const std::string& dependency : manifest.dependencies) {
        if (!append_package_dependency_order(base_root, override_roots, dependency,
                max_dependencies, visiting, visited, order, error)) return false;
    }
    visiting.erase(package_name);
    visited.insert(package_name);
    order.push_back(package_name);
    return true;
}

inline bool package_dependency_order(const std::filesystem::path& base_root,
    const std::vector<std::filesystem::path>& override_roots, const std::string& package_name,
    size_t max_dependencies, std::vector<std::string>& order, std::string& error) {
    order.clear();
    error.clear();
    if (!safe_package_path(package_name)) {
        error = "unsafe package name";
        return false;
    }
    const PackageResolution resolved = resolve_package_path(
        base_root, override_roots, package_name, 1);
    if (!resolved.found) {
        error = resolved.error;
        return false;
    }
    const BinaryPackageManifest root_manifest = read_binary_package_manifest(resolved.path);
    if (!root_manifest.valid) {
        error = root_manifest.error;
        return false;
    }
    std::set<std::string> visiting{package_name};
    std::set<std::string> visited;
    for (const std::string& dependency : root_manifest.dependencies) {
        if (!append_package_dependency_order(base_root, override_roots, dependency,
                max_dependencies, visiting, visited, order, error)) {
            order.clear();
            return false;
        }
    }
    return true;
}

} // namespace probe
