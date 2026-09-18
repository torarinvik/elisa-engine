#pragma once
// zstd integration for package compression. The plan lists zstd for package
// compression; here the actual cooked package file is compressed and
// decompressed so the ratio is a measurement of a real payload rather than a
// synthetic buffer, and the round trip must reproduce the file byte for byte.
#include "probe_core.h"

#include <zstd.h>

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace probe {

inline bool probe_zstd(const std::string& package_path) {
    std::ifstream input(package_path, std::ios::binary);
    if (!check(input.good(), "zstd package readable")) {
        return false;
    }
    const std::vector<char> source((std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());
    if (!check(!source.empty(), "zstd package non-empty")) {
        return false;
    }
    std::vector<char> compressed(ZSTD_compressBound(source.size()));
    const size_t compressed_size = ZSTD_compress(compressed.data(), compressed.size(),
        source.data(), source.size(), 3);
    if (!check(!ZSTD_isError(compressed_size), "zstd compress")) {
        return false;
    }
    std::vector<char> restored(source.size());
    const size_t restored_size = ZSTD_decompress(restored.data(), restored.size(),
        compressed.data(), compressed_size);
    if (!check(!ZSTD_isError(restored_size) && restored_size == source.size() && restored == source,
            "zstd round trip reproduces the package")) {
        return false;
    }
    const double ratio = (double)compressed_size / (double)source.size();
    std::fprintf(stdout, "zstd: source=%zu compressed=%zu ratio=%.3f\n",
        source.size(), compressed_size, ratio);
    return true;
}

} // namespace probe
