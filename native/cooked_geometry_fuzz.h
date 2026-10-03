// A12: seeded mutation corpus for the cooked geometry loader. Each mutant is
// the accepted package with byte flips, extreme 32-bit fields, a truncation or
// a duplicated span. The loader must either refuse it with an error message or
// return geometry that holds the stream invariants; an accepted mutant that
// breaks one is counted as broken. The verdict digest makes a run reproducible.
#pragma once

#include "cooked_geometry_package.h"

#include <cstdint>
#include <vector>

namespace elisa::assets::fuzz {

struct GeometryFuzzResult {
    uint32_t accepted = 0;
    uint32_t refused = 0;
    uint32_t broken = 0;
    uint64_t digest = 0;
};

inline uint32_t next_seed(uint32_t& seed) {
    seed = seed * 1664525u + 1013904223u;
    return seed >> 8;
}

inline bool geometry_holds(const CookedGeometry& geometry) {
    const size_t vertices = geometry.positions.size() / 3;
    if (geometry.positions.empty() || geometry.positions.size() % 3 != 0) return false;
    if (geometry.normals.size() != geometry.positions.size()) return false;
    if (geometry.uvs.size() != vertices * 2) return false;
    if (geometry.indices.empty() || geometry.indices.size() % 3 != 0) return false;
    for (uint32_t index : geometry.indices) {
        if (index >= vertices) return false;
    }
    return true;
}

inline std::vector<uint8_t> mutate(const std::vector<uint8_t>& base, uint32_t& seed) {
    std::vector<uint8_t> bytes = base;
    const uint32_t kind = next_seed(seed) % 4;
    const size_t at = next_seed(seed) % bytes.size();
    if (kind == 0) {
        for (uint32_t flips = 1 + next_seed(seed) % 4; flips > 0; --flips) {
            bytes[next_seed(seed) % bytes.size()] ^= uint8_t(1u << (next_seed(seed) % 8));
        }
    } else if (kind == 1) {
        static const uint32_t extremes[] = {0u, 1u, 0x7fffffffu, 0x80000000u, 0xffffffffu};
        const uint32_t value = extremes[next_seed(seed) % 5];
        for (size_t k = 0; k < 4 && at + k < bytes.size(); ++k) bytes[at + k] = uint8_t(value >> (8 * k));
    } else if (kind == 2) {
        bytes.resize(at == 0 ? 1 : at);
    } else {
        const size_t length = 1 + next_seed(seed) % 64;
        const size_t stop = at + length < bytes.size() ? at + length : bytes.size();
        bytes.insert(bytes.begin() + long(at), base.begin() + long(at), base.begin() + long(stop));
    }
    return bytes;
}

inline GeometryFuzzResult fuzz_cooked_geometry(const std::vector<uint8_t>& base, uint32_t seed, uint32_t rounds) {
    GeometryFuzzResult result;
    for (uint32_t round = 0; round < rounds; ++round) {
        const std::vector<uint8_t> bytes = mutate(base, seed);
        CookedGeometry geometry;
        std::string error;
        uint64_t code = 0;
        if (load_cooked_geometry_bytes(bytes.data(), bytes.size(), geometry, error)) {
            code = geometry_holds(geometry) ? 1 : 3;
            if (code == 1) ++result.accepted; else ++result.broken;
        } else {
            code = error.empty() ? 3 : 2;
            if (code == 2) ++result.refused; else ++result.broken;
        }
        result.digest = (result.digest * 31 + code) % 1000000007ull;
    }
    return result;
}

} // namespace elisa::assets::fuzz
