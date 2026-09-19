#pragma once

#include <fstream>
#include <map>
#include <string>

namespace probe {

struct PackageIndex {
    static constexpr size_t MAX_PACKAGE_BYTES = 64 * 1024 * 1024;
    static constexpr size_t MAX_LINE_BYTES = 16 * 1024 * 1024;
    static constexpr size_t MAX_SECTIONS = 128;

    std::map<std::string, std::string> sections;
    bool valid = false;
    std::string error;
};

inline bool safe_package_path(const std::string& value) {
    if (value.empty() || value.front() == '/' || value.find('\\') != std::string::npos) {
        return false;
    }
    size_t start = 0;
    while (start < value.size()) {
        const size_t end = value.find('/', start);
        const std::string component = value.substr(start, end == std::string::npos ? end : end - start);
        if (component.empty() || component == "..") {
            return false;
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return true;
}

inline PackageIndex read_package_index(const std::string& path) {
    PackageIndex package;
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) { package.error = "missing package"; return package; }
    const std::streamoff size = input.tellg();
    if (size < 0 || static_cast<size_t>(size) > PackageIndex::MAX_PACKAGE_BYTES) {
        package.error = "package exceeds size bound"; return package;
    }
    input.seekg(0);
    std::string bytes(static_cast<size_t>(size), '\0');
    input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!input && !bytes.empty()) { package.error = "package read failed"; return package; }
    size_t offset = 0;
    while (offset < bytes.size()) {
        const size_t newline = bytes.find('\n', offset);
        const size_t end = newline == std::string::npos ? bytes.size() : newline;
        if (end - offset > PackageIndex::MAX_LINE_BYTES) { package.error = "section exceeds size bound"; return package; }
        const std::string line = bytes.substr(offset, end - offset);
        const size_t separator = line.find('=');
        if (separator == std::string::npos || separator == 0) { package.error = "malformed section"; return package; }
        const std::string key = line.substr(0, separator);
        if (key.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_") != std::string::npos ||
            package.sections.count(key) != 0 || package.sections.size() >= PackageIndex::MAX_SECTIONS) {
            package.error = "invalid or duplicate section"; return package;
        }
        package.sections.emplace(key, line.substr(separator + 1));
        if (newline == std::string::npos) break;
        offset = newline + 1;
    }
    const auto source = package.sections.find("source");
    if (source != package.sections.end() && !safe_package_path(source->second)) {
        package.error = "unsafe package path"; return package;
    }
    package.valid = true;
    return package;
}

} // namespace probe
