// Checks the production cooked-geometry loader against a manifest of packages.
// Each tab-separated manifest line is either
//   accept <path> <index count> <material slots> (<start> <count> <slot>)...
//   reject <path> <expected error text>
// and the loader must accept with exactly those subsets, or reject with an
// error that contains the expected text.
#include "cooked_geometry_package.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::vector<std::string> split_tabs(const std::string& line) {
    std::vector<std::string> fields;
    std::stringstream stream(line);
    std::string field;
    while (std::getline(stream, field, '\t')) fields.push_back(field);
    return fields;
}

bool check_accepted(const std::vector<std::string>& fields, const elisa::assets::CookedGeometry& geometry) {
    if (fields.size() < 4 || (fields.size() - 4) % 3 != 0) return false;
    if (geometry.indices.size() != std::stoul(fields[2]) ||
        geometry.material_slots != std::stoul(fields[3]) ||
        geometry.subsets.size() != (fields.size() - 4) / 3) return false;
    for (size_t subset = 0; subset < geometry.subsets.size(); ++subset) {
        const auto& actual = geometry.subsets[subset];
        if (actual.index_start != std::stoul(fields[4 + subset * 3]) ||
            actual.index_count != std::stoul(fields[5 + subset * 3]) ||
            actual.material_slot != std::stoul(fields[6 + subset * 3])) return false;
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    std::ifstream manifest(argv[1]);
    if (!manifest) return 2;
    std::string line;
    int cases = 0;
    int failures = 0;
    while (std::getline(manifest, line)) {
        const std::vector<std::string> fields = split_tabs(line);
        if (fields.size() < 3) return 2;
        elisa::assets::CookedGeometry geometry;
        std::string error;
        const bool loaded = elisa::assets::load_cooked_geometry_asset(fields[1], geometry, error);
        bool passed = false;
        if (fields[0] == "accept") {
            passed = loaded && check_accepted(fields, geometry);
        } else if (fields[0] == "reject") {
            passed = !loaded && error.find(fields[2]) != std::string::npos;
        } else {
            return 2;
        }
        ++cases;
        if (!passed) {
            ++failures;
            std::fprintf(stderr, "subset case failed: %s %s (loaded=%d, error=%s)\n",
                fields[0].c_str(), fields[1].c_str(), loaded ? 1 : 0, error.c_str());
        }
    }
    if (cases == 0) return 2;
    std::printf("geometry subset loader: %d cases, %d failed\n", cases, failures);
    return failures == 0 ? 0 : 1;
}
