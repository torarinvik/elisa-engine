#pragma once

#include "package_load.h"
#include "probe_support.h"
#include "virtual_file_service.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <vector>
#include <zstd.h>

namespace probe {

inline void put_package_u16(std::vector<uint8_t>& bytes, size_t offset, uint16_t value) {
    bytes[offset] = static_cast<uint8_t>(value);
    bytes[offset + 1] = static_cast<uint8_t>(value >> 8);
}

inline void put_package_u64(std::vector<uint8_t>& bytes, size_t offset, uint64_t value) {
    for (size_t index = 0; index < 8; ++index) bytes[offset + index] = static_cast<uint8_t>(value >> (index * 8));
}

inline void write_binary_package_fixture(const std::filesystem::path& path, bool overlap,
    bool invalid_compression = false, bool compressed = false) {
    const uint16_t count = overlap ? 2 : 1;
    const size_t index_end = BinaryPackageIndex::HEADER_BYTES + count * BinaryPackageIndex::ENTRY_BYTES;
    const std::string payload = "elisa-bundle-section";
    std::vector<uint8_t> encoded(payload.begin(), payload.end());
    if (compressed) {
        const size_t bound = ZSTD_compressBound(payload.size());
        encoded.resize(bound);
        const size_t size = ZSTD_compress(encoded.data(), encoded.size(), payload.data(), payload.size(), 1);
        encoded.resize(size);
    }
    const size_t file_size = index_end + (overlap ? 16 : encoded.size());
    std::vector<uint8_t> bytes(file_size, 0);
    bytes[0] = 'E'; bytes[1] = 'L'; bytes[2] = 'P'; bytes[3] = 'K';
    put_package_u16(bytes, 4, 1);
    put_package_u16(bytes, 6, count);
    put_package_u64(bytes, 8, BinaryPackageIndex::HEADER_BYTES);
    put_package_u64(bytes, 16, count * BinaryPackageIndex::ENTRY_BYTES);
    for (uint16_t index = 0; index < count; ++index) {
        const size_t entry = BinaryPackageIndex::HEADER_BYTES + index * BinaryPackageIndex::ENTRY_BYTES;
        const std::string name = index == 0 ? "mesh" : "texture";
        for (size_t character = 0; character < name.size(); ++character) bytes[entry + character] = name[character];
        const uint64_t offset = overlap ? index_end : index_end;
        put_package_u64(bytes, entry + 16, offset);
        put_package_u64(bytes, entry + 24, overlap ? 16 : encoded.size());
        put_package_u64(bytes, entry + 32, overlap ? 16 : payload.size());
        bytes[entry + 40] = invalid_compression ? 2 : (compressed ? 1 : 0);
        if (index == 0) {
            const size_t copy_size = std::min(encoded.size(), overlap ? size_t(16) : encoded.size());
            std::copy(encoded.begin(), encoded.begin() + copy_size, bytes.begin() + offset);
        }
        if (overlap && index == 1) bytes[offset + 1] = 0xA5;
    }
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

inline bool probe_package_bounds(const std::string& valid_package) {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "elisa-package-probe";
    std::filesystem::create_directories(root);
    const std::filesystem::path duplicate = root / "duplicate.pkg";
    std::ofstream(duplicate) << "format=elisa-cooked-v2\nformat=other\n";
    const std::filesystem::path traversal = root / "traversal.pkg";
    std::ofstream(traversal) << "format=elisa-cooked-v2\nsource=../outside.pkg\n";
    const std::filesystem::path malformed = root / "malformed.pkg";
    std::ofstream(malformed) << "not-a-section\n";
    const std::filesystem::path binary = root / "valid.elpk";
    const std::filesystem::path overlap = root / "overlap.elpk";
    const std::filesystem::path compressed = root / "compression.elpk";
    const std::filesystem::path zstd = root / "zstd.elpk";
    const std::filesystem::path base_root = root / "base";
    const std::filesystem::path override_root = root / "override";
    std::filesystem::create_directories(base_root);
    std::filesystem::create_directories(override_root);
    std::ofstream(base_root / "maze.elpk") << "base";
    std::ofstream(override_root / "maze.elpk") << "override";
    write_binary_package_fixture(binary, false);
    write_binary_package_fixture(overlap, true);
    write_binary_package_fixture(compressed, false, true);
    write_binary_package_fixture(zstd, false, false, true);
    write_binary_package_fixture(base_root / "maze.elpk", false);
    write_binary_package_fixture(override_root / "maze.elpk", false, false, true);
    const BinaryPackageIndex zstd_index = read_binary_package_index(zstd.string());
    std::vector<uint8_t> section;
    std::string section_error;
    VirtualFileService vfs;
    const bool mounted = vfs.mount(base_root, {override_root}, 7);
    const VirtualReadHandle first_read = vfs.request("maze.elpk", "mesh");
    const VirtualReadHandle duplicate_read = vfs.request("maze.elpk", "mesh");
    std::vector<uint8_t> virtual_bytes;
    std::string virtual_error;
    uint64_t virtual_generation = 0;
    const bool virtual_read = mounted && first_read.slot == duplicate_read.slot &&
        vfs.pump(1) == 1 && vfs.state(first_read) == VirtualReadState::Ready &&
        vfs.take(first_read, virtual_bytes, virtual_generation, virtual_error) &&
        virtual_generation == 7 && std::string(virtual_bytes.begin(), virtual_bytes.end()) == "elisa-bundle-section";
    const VirtualReadHandle cancelled = vfs.request("maze.elpk", "missing");
    const bool cancelled_read = vfs.cancel(cancelled) && vfs.state(cancelled) == VirtualReadState::Cancelled;
    const VirtualReadHandle stale = vfs.request("maze.elpk", "mesh");
    const bool remounted = vfs.mount(base_root, {}, 8);
    const bool stale_read = remounted && vfs.pump(1) == 1 && vfs.state(stale) == VirtualReadState::Failed;
    const bool result = check(load_cooked_package(valid_package).loaded, "bounded package load") &&
        check(!load_cooked_package(duplicate.string()).loaded, "duplicate package section rejected") &&
        check(!load_cooked_package(traversal.string()).loaded, "package traversal rejected") &&
        check(!load_cooked_package(malformed.string()).loaded, "malformed package rejected") &&
        check(read_binary_package_index(binary.string()).valid, "binary package index") &&
        check(!read_binary_package_index(overlap.string()).valid, "binary package overlap rejected") &&
        check(!read_binary_package_index(compressed.string()).valid, "binary package compression rejected") &&
        check(read_binary_package_section(zstd.string(), zstd_index, "mesh", section, section_error) &&
            std::string(section.begin(), section.end()) == "elisa-bundle-section", "zstd binary section read") &&
        check(resolve_package_path(base_root, {override_root}, "maze.elpk", 7).found &&
            resolve_package_path(base_root, {override_root}, "maze.elpk", 7).override_used &&
            resolve_package_path(base_root, {override_root}, "maze.elpk", 7).generation == 7, "package override resolution") &&
        check(resolve_package_path(base_root, {}, "maze.elpk", 8).found &&
            !resolve_package_path(base_root, {}, "maze.elpk", 8).override_used, "package base resolution") &&
        check(!resolve_package_path(base_root, {}, "../maze.elpk", 8).found, "package override traversal rejected") &&
        check(!resolve_package_path(base_root, {}, "maze.elpk", 0).found, "package zero generation rejected") &&
        check(virtual_read, "virtual file read coalescing") &&
        check(cancelled_read, "virtual file cancellation") &&
        check(stale_read, "virtual file generation invalidation");
    std::filesystem::remove_all(root);
    return result;
}

} // namespace probe
