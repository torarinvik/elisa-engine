#pragma once
// Runtime side of the asset pipeline: load a cooked package and expose what
// it carries. The package is produced offline (scripts/cook_assets.py), so
// the shipped runtime never parses source formats; it reads normalized,
// versioned data. Integrity (the source hash) is checked by the cooking tool
// and the validation record; the runtime's job is to refuse a package whose
// format it does not know and whose counts disagree with its own import.
#include <fstream>
#include <string>

namespace probe {

struct CookedPackage {
    std::string format;
    long long triangles = -1;
    long long positions = -1;
    bool loaded = false;
};

inline CookedPackage load_cooked_package(const std::string& path) {
    CookedPackage package;
    std::ifstream input(path);
    if (!input) {
        return package;
    }
    std::string line;
    while (std::getline(input, line)) {
        const auto separator = line.find('=');
        if (separator == std::string::npos) {
            continue;
        }
        const std::string key = line.substr(0, separator);
        const std::string value = line.substr(separator + 1);
        if (key == "format") {
            package.format = value;
        } else if (key == "triangles") {
            package.triangles = std::stoll(value);
        } else if (key == "positions") {
            package.positions = std::stoll(value);
        }
    }
    package.loaded = package.format == "elisa-cooked-v1";
    return package;
}

} // namespace probe
