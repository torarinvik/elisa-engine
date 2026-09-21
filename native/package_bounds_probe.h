#pragma once

#include "package_load.h"
#include "cooked_geometry_package.h"
#include "probe_support.h"
#include "virtual_file_service.h"
#include "native_resource_loader.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>
#include <zstd.h>

namespace probe {

inline void put_package_u16(std::vector<uint8_t>& bytes, size_t offset, uint16_t value) {
    bytes[offset] = static_cast<uint8_t>(value);
    bytes[offset + 1] = static_cast<uint8_t>(value >> 8);
}

inline void put_package_u32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
    for (size_t index = 0; index < 4; ++index) bytes[offset + index] = static_cast<uint8_t>(value >> (index * 8));
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
        put_package_u32(bytes, entry + 44, package_crc32(
            reinterpret_cast<const uint8_t*>(payload.data()), payload.size()));
        if (index == 0) {
            const size_t copy_size = std::min(encoded.size(), overlap ? size_t(16) : encoded.size());
            std::copy(encoded.begin(), encoded.begin() + copy_size, bytes.begin() + offset);
        }
        if (overlap && index == 1) bytes[offset + 1] = 0xA5;
    }
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

// One zstd section that expands to 80 MiB of zeros from a few KiB. Its index
// entry either admits that size, over the 64 MiB bound, or claims 4 KiB.
inline void write_zstd_bomb_package_fixture(const std::filesystem::path& path, uint64_t declared_size) {
    const std::vector<uint8_t> payload(80 * 1024 * 1024, 0);
    std::vector<uint8_t> encoded(ZSTD_compressBound(payload.size()));
    encoded.resize(ZSTD_compress(encoded.data(), encoded.size(), payload.data(), payload.size(), 1));
    const size_t index_end = BinaryPackageIndex::HEADER_BYTES + BinaryPackageIndex::ENTRY_BYTES;
    std::vector<uint8_t> bytes(index_end + encoded.size(), 0);
    bytes[0] = 'E'; bytes[1] = 'L'; bytes[2] = 'P'; bytes[3] = 'K';
    put_package_u16(bytes, 4, 1);
    put_package_u16(bytes, 6, 1);
    put_package_u64(bytes, 8, BinaryPackageIndex::HEADER_BYTES);
    put_package_u64(bytes, 16, BinaryPackageIndex::ENTRY_BYTES);
    const size_t entry = BinaryPackageIndex::HEADER_BYTES;
    const std::string name = "mesh";
    std::copy(name.begin(), name.end(), bytes.begin() + entry);
    put_package_u64(bytes, entry + 16, index_end);
    put_package_u64(bytes, entry + 24, encoded.size());
    put_package_u64(bytes, entry + 32, declared_size);
    put_package_u32(bytes, entry + 40, 1);
    put_package_u32(bytes, entry + 44, package_crc32(payload.data(), payload.size()));
    std::copy(encoded.begin(), encoded.end(), bytes.begin() + index_end);
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

inline void write_large_binary_package_fixture(const std::filesystem::path& path, size_t payload_size) {
    const size_t index_end = BinaryPackageIndex::HEADER_BYTES + BinaryPackageIndex::ENTRY_BYTES;
    std::vector<uint8_t> header(index_end, 0);
    const std::vector<uint8_t> payload(payload_size, 0x5A);
    header[0] = 'E'; header[1] = 'L'; header[2] = 'P'; header[3] = 'K';
    put_package_u16(header, 4, 1);
    put_package_u16(header, 6, 1);
    put_package_u64(header, 8, BinaryPackageIndex::HEADER_BYTES);
    put_package_u64(header, 16, BinaryPackageIndex::ENTRY_BYTES);
    const size_t entry = BinaryPackageIndex::HEADER_BYTES;
    const std::string name = "payload";
    std::copy(name.begin(), name.end(), header.begin() + entry);
    put_package_u64(header, entry + 16, index_end);
    put_package_u64(header, entry + 24, payload.size());
    put_package_u64(header, entry + 32, payload.size());
    put_package_u32(header, entry + 44, package_crc32(payload.data(), payload.size()));
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));
    output.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
}

inline void write_binary_package_with_manifest(const std::filesystem::path& path,
    const std::vector<std::string>& dependencies, bool compressed = false) {
    const std::string payload = "elisa-bundle-section";
    std::vector<uint8_t> encoded(payload.begin(), payload.end());
    if (compressed) {
        const size_t bound = ZSTD_compressBound(payload.size());
        encoded.resize(bound);
        const size_t size = ZSTD_compress(encoded.data(), encoded.size(), payload.data(), payload.size(), 1);
        encoded.resize(size);
    }
    std::string manifest = "ELISA-PACKAGE-MANIFEST-1\n";
    for (const std::string& dependency : dependencies) manifest += "dependency=" + dependency + "\n";
    const size_t index_end = BinaryPackageIndex::HEADER_BYTES + 2 * BinaryPackageIndex::ENTRY_BYTES;
    const size_t manifest_offset = (index_end + encoded.size() + 15) & ~size_t(15);
    std::vector<uint8_t> bytes(manifest_offset + manifest.size(), 0);
    bytes[0] = 'E'; bytes[1] = 'L'; bytes[2] = 'P'; bytes[3] = 'K';
    put_package_u16(bytes, 4, 1);
    put_package_u16(bytes, 6, 2);
    put_package_u64(bytes, 8, BinaryPackageIndex::HEADER_BYTES);
    put_package_u64(bytes, 16, 2 * BinaryPackageIndex::ENTRY_BYTES);
    const auto write_entry = [&bytes](size_t index, const std::string& name, uint64_t offset,
        uint64_t size, uint64_t unpacked_size, uint32_t compression, uint32_t checksum) {
        const size_t entry = BinaryPackageIndex::HEADER_BYTES + index * BinaryPackageIndex::ENTRY_BYTES;
        std::copy(name.begin(), name.end(), bytes.begin() + entry);
        put_package_u64(bytes, entry + 16, offset);
        put_package_u64(bytes, entry + 24, size);
        put_package_u64(bytes, entry + 32, unpacked_size);
        put_package_u32(bytes, entry + 40, compression);
        put_package_u32(bytes, entry + 44, checksum);
    };
    write_entry(0, "mesh", index_end, encoded.size(), payload.size(), compressed ? 1 : 0,
        package_crc32(reinterpret_cast<const uint8_t*>(payload.data()), payload.size()));
    write_entry(1, "manifest", manifest_offset, manifest.size(), manifest.size(), 0,
        package_crc32(reinterpret_cast<const uint8_t*>(manifest.data()), manifest.size()));
    std::copy(encoded.begin(), encoded.end(), bytes.begin() + index_end);
    std::copy(manifest.begin(), manifest.end(), bytes.begin() + manifest_offset);
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

inline bool probe_package_bounds(const std::string& valid_package,
    wi::graphics::GraphicsDevice* device = nullptr) {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "elisa-package-probe";
    std::filesystem::create_directories(root);
    const std::filesystem::path duplicate = root / "duplicate.pkg";
    std::ofstream(duplicate) << "format=elisa-cooked-v2\nformat=other\n";
    const std::filesystem::path traversal = root / "traversal.pkg";
    std::ofstream(traversal) << "format=elisa-cooked-v2\nsource=../outside.pkg\n";
    const std::filesystem::path malformed = root / "malformed.pkg";
    std::ofstream(malformed) << "not-a-section\n";
    const std::filesystem::path uv_package_path = root / "uv-package.pkg";
    {
        std::ofstream uv_package(uv_package_path);
        uv_package << "format=elisa-cooked-v2\nsource=triangle.fbx\ntriangles=1\n"
            << "position_stride=12\nnormal_stride=12\nuv_stride=8\nindex_stride=4\n"
            << "positions=3\nindices=3\npositions_b64=" << std::string(48, 'A')
            << "\nnormals_b64=" << std::string(48, 'A') << "\nuvs_b64="
            << std::string(32, 'A') << "\nindices_b64=AAAAAAEAAAACAAAA\n";
    }
    const CookedPackage uv_package = load_cooked_package(uv_package_path.string());
    elisa::assets::CookedGeometry runtime_geometry;
    std::string runtime_geometry_error;
    const bool runtime_geometry_loaded = elisa::assets::load_cooked_geometry(
        uv_package_path.string(), runtime_geometry, runtime_geometry_error);
    const std::filesystem::path binary = root / "valid.elpk";
    const std::filesystem::path overlap = root / "overlap.elpk";
    const std::filesystem::path compressed = root / "compression.elpk";
    const std::filesystem::path zstd = root / "zstd.elpk";
    const std::filesystem::path corrupt = root / "corrupt.elpk";
    const std::filesystem::path base_root = root / "base";
    const std::filesystem::path override_root = root / "override";
    std::filesystem::create_directories(base_root);
    std::filesystem::create_directories(override_root);
    const std::filesystem::path outside_package = root / "outside.elpk";
    std::ofstream(outside_package) << "outside";
    std::ofstream(base_root / "maze.elpk") << "base";
    std::ofstream(override_root / "maze.elpk") << "override";
    write_binary_package_fixture(binary, false);
    write_binary_package_fixture(overlap, true);
    write_binary_package_fixture(compressed, false, true);
    write_binary_package_fixture(zstd, false, false, true);
    write_binary_package_fixture(corrupt, false);
    {
        std::fstream file(corrupt, std::ios::binary | std::ios::in | std::ios::out);
        const size_t payload_offset = BinaryPackageIndex::HEADER_BYTES + BinaryPackageIndex::ENTRY_BYTES;
        file.seekg(static_cast<std::streamoff>(payload_offset));
        char byte = 0;
        file.read(&byte, 1);
        byte ^= 0x01;
        file.seekp(static_cast<std::streamoff>(payload_offset));
        file.write(&byte, 1);
    }
    std::filesystem::copy_file(corrupt, base_root / "corrupt.elpk",
        std::filesystem::copy_options::overwrite_existing);
    std::error_code symlink_error;
    std::filesystem::remove(override_root / "escape.elpk", symlink_error);
    symlink_error.clear();
    std::filesystem::create_symlink(outside_package, override_root / "escape.elpk", symlink_error);
    write_binary_package_with_manifest(base_root / "maze.elpk", {"dep.elpk"});
    write_binary_package_with_manifest(override_root / "maze.elpk", {"dep.elpk"}, true);
    write_binary_package_with_manifest(base_root / "dep.elpk", {});
    write_binary_package_with_manifest(base_root / "missing-dep.elpk", {"missing.elpk"});
    write_binary_package_with_manifest(base_root / "leaf-a.elpk", {});
    write_binary_package_with_manifest(base_root / "leaf-z.elpk", {});
    write_binary_package_with_manifest(base_root / "branch.elpk", {"leaf-z.elpk"});
    write_binary_package_with_manifest(base_root / "graph.elpk", {"branch.elpk", "leaf-a.elpk"});
    write_binary_package_with_manifest(base_root / "cycle-a.elpk", {"cycle-b.elpk"});
    write_binary_package_with_manifest(base_root / "cycle-b.elpk", {"cycle-a.elpk"});
    // chain-k.elpk depends on chain-(k+1).elpk, so chain-1 has 16 transitive
    // dependencies and chain-0 has 17.
    for (int link = 0; link <= 17; ++link) {
        write_binary_package_with_manifest(base_root / ("chain-" + std::to_string(link) + ".elpk"),
            link == 17 ? std::vector<std::string>{} :
                std::vector<std::string>{"chain-" + std::to_string(link + 1) + ".elpk"});
    }
    // Manifest names are relative to the declaring package's directory.
    std::filesystem::create_directories(base_root / "nested/textures");
    write_binary_package_with_manifest(base_root / "nested/root.elpk", {"textures/leaf.elpk"});
    write_binary_package_with_manifest(base_root / "nested/textures/leaf.elpk", {"detail.elpk"});
    write_binary_package_with_manifest(base_root / "nested/textures/detail.elpk", {});
    std::vector<std::string> chain_order;
    std::string nested_error;
    const bool nested_names_resolve = package_dependency_order(base_root, {}, "nested/root.elpk",
        BinaryPackageManifest::MAX_DEPENDENCIES, chain_order, nested_error) &&
        chain_order == std::vector<std::string>{"nested/textures/detail.elpk", "nested/textures/leaf.elpk"};
    std::string chain_error;
    const bool chain_of_sixteen = package_dependency_order(base_root, {}, "chain-1.elpk",
        BinaryPackageManifest::MAX_DEPENDENCIES, chain_order, chain_error) &&
        chain_order.size() == 16 && chain_order.front() == "chain-17.elpk";
    const bool chain_of_seventeen_rejected = !package_dependency_order(base_root, {}, "chain-0.elpk",
        BinaryPackageManifest::MAX_DEPENDENCIES, chain_order, chain_error) &&
        chain_error == "package dependency count exceeded" && chain_order.empty();
    const std::filesystem::path admitted_bomb = base_root / "bomb-admitted.elpk";
    const std::filesystem::path hidden_bomb = base_root / "bomb-hidden.elpk";
    write_zstd_bomb_package_fixture(admitted_bomb, 80 * 1024 * 1024);
    write_zstd_bomb_package_fixture(hidden_bomb, 4096);
    const BinaryPackageIndex hidden_bomb_index = read_binary_package_index(hidden_bomb.string());
    std::vector<uint8_t> bomb_section;
    std::string bomb_error;
    // The output buffer is the declared 4 KiB; zstd stops at it instead of growing.
    const bool bombs_rejected = !read_binary_package_index(admitted_bomb.string()).valid &&
        hidden_bomb_index.valid &&
        !read_binary_package_section(hidden_bomb.string(), hidden_bomb_index, "mesh", bomb_section, bomb_error) &&
        bomb_section.empty() && bomb_error == "zstd section decompression failed";
    const BinaryPackageIndex zstd_index = read_binary_package_index(zstd.string());
    const BinaryPackageIndex corrupt_index = read_binary_package_index(corrupt.string());
    BinaryPackageManifest unsorted_manifest;
    const std::string unsorted_manifest_text =
        "ELISA-PACKAGE-MANIFEST-1\ndependency=z.elpk\ndependency=a.elpk\n";
    const std::vector<uint8_t> unsorted_manifest_bytes(unsorted_manifest_text.begin(), unsorted_manifest_text.end());
    const bool unsorted_manifest_rejected = !parse_binary_package_manifest(
        unsorted_manifest_bytes, unsorted_manifest) &&
        unsorted_manifest.error == "package manifest dependency order rejected";
    const PackageResolution symlink_escape = resolve_package_path(
        base_root, {override_root}, "escape.elpk", 7);
    bool symlink_escape_ok = true;
    if (symlink_error) {
        std::fprintf(stderr, "wicked probe skipped: package symlink escape test unavailable: %s\n",
            symlink_error.message().c_str());
    } else {
        symlink_escape_ok = check(!symlink_escape.found &&
            symlink_escape.error == "package resolves outside mount root",
            "package symlink escape rejected");
    }
    std::vector<uint8_t> section;
    std::string section_error;
    std::vector<uint8_t> corrupt_section;
    std::string corrupt_error;
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
    VirtualFileService worker_vfs;
    const bool worker_mounted = worker_vfs.mount(base_root, {override_root}, 10);
    const VirtualReadHandle worker_read = worker_vfs.request_with_dependencies(
        "maze.elpk", "mesh", {"dep.elpk"}, 10);
    auto worker = worker_vfs.pump_async(1);
    const bool worker_read_ok = worker_mounted && worker_read.generation != 0 && worker.get() == 1 &&
        worker_vfs.state(worker_read) == VirtualReadState::Ready;
    const VirtualReadHandle missing_dependency_read = worker_vfs.request_with_dependencies(
        "missing-dep.elpk", "mesh", {"missing.elpk"}, 10);
    const bool dependency_rejections = missing_dependency_read.generation != 0 &&
        worker_vfs.request_with_dependencies("maze.elpk", "mesh", {"dep.elpk", "dep.elpk"}, 10).generation == 0 &&
        worker_vfs.request_with_dependencies("maze.elpk", "mesh", {"maze.elpk"}, 10).generation == 0 &&
        worker_vfs.pump(1) == 1 && worker_vfs.state(missing_dependency_read) == VirtualReadState::Failed &&
        worker_vfs.error(missing_dependency_read) == "package dependency is missing";
    const std::filesystem::path in_flight_path = base_root / "in-flight.elpk";
    write_large_binary_package_fixture(in_flight_path, 32 * 1024 * 1024);
    VirtualFileService in_flight_vfs;
    const bool in_flight_mounted = in_flight_vfs.mount(base_root, {}, 12);
    const VirtualReadHandle in_flight_read = in_flight_vfs.request("in-flight.elpk", "payload");
    auto in_flight_worker = in_flight_vfs.pump_async(1);
    const auto in_flight_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    VirtualReadState observed_in_flight_state = VirtualReadState::Queued;
    while (std::chrono::steady_clock::now() < in_flight_deadline) {
        observed_in_flight_state = in_flight_vfs.state(in_flight_read);
        if (observed_in_flight_state != VirtualReadState::Queued) break;
        std::this_thread::yield();
    }
    const bool saw_in_flight_read = in_flight_mounted &&
        observed_in_flight_state == VirtualReadState::Reading;
    const bool cancelled_in_flight = saw_in_flight_read && in_flight_vfs.cancel(in_flight_read) &&
        in_flight_vfs.state(in_flight_read) == VirtualReadState::Cancelled &&
        in_flight_worker.get() == 1 && in_flight_vfs.state(in_flight_read) == VirtualReadState::Cancelled;
    std::future<uint32_t> shutdown_worker;
    bool shutdown_worker_scheduled = false;
    {
        VirtualFileService shutdown_vfs;
        shutdown_worker_scheduled = shutdown_vfs.mount(base_root, {}, 13) &&
            shutdown_vfs.request("in-flight.elpk", "payload").generation != 0;
        shutdown_worker = shutdown_vfs.pump_async(1);
    }
    const bool worker_shutdown_waits = shutdown_worker_scheduled &&
        shutdown_worker.wait_for(std::chrono::seconds(0)) == std::future_status::ready &&
        shutdown_worker.get() == 1;
    VirtualFileService remount_vfs;
    const bool first_epoch_mounted = remount_vfs.mount(base_root, {}, 14);
    const VirtualReadHandle prior_epoch_read = remount_vfs.request("dep.elpk", "mesh");
    const bool second_epoch_mounted = remount_vfs.mount(base_root, {}, 14);
    const VirtualReadHandle current_epoch_read = remount_vfs.request("dep.elpk", "mesh");
    const bool remount_epoch_invalidates = first_epoch_mounted && second_epoch_mounted &&
        current_epoch_read.generation != 0 && current_epoch_read.slot != prior_epoch_read.slot &&
        remount_vfs.pump(2) == 2 && remount_vfs.state(prior_epoch_read) == VirtualReadState::Failed &&
        remount_vfs.error(prior_epoch_read) == "stale mount generation" &&
        remount_vfs.state(current_epoch_read) == VirtualReadState::Ready;
    VirtualFileService graph_vfs;
    const bool graph_mounted = graph_vfs.mount(base_root, {}, 11);
    const VirtualReadHandle graph_read = graph_vfs.request_with_dependencies("graph.elpk", "mesh",
        {"leaf-z.elpk", "branch.elpk", "leaf-a.elpk"}, 11);
    const VirtualReadHandle wrong_order_read = graph_vfs.request_with_dependencies("graph.elpk", "mesh",
        {"leaf-a.elpk", "branch.elpk", "leaf-z.elpk"}, 11);
    const VirtualReadHandle cyclic_read = graph_vfs.request_with_dependencies("cycle-a.elpk", "mesh",
        {"cycle-b.elpk"}, 11);
    const bool graph_pumped = graph_mounted && graph_vfs.pump(3) == 3;
    const bool dependency_graph = graph_pumped &&
        graph_vfs.state(graph_read) == VirtualReadState::Ready &&
        graph_vfs.state(wrong_order_read) == VirtualReadState::Failed &&
        graph_vfs.error(wrong_order_read) == "package dependency order mismatch" &&
        graph_vfs.state(cyclic_read) == VirtualReadState::Failed &&
        graph_vfs.error(cyclic_read) == "package dependency cycle";
    const VirtualReadHandle cancelled = vfs.request("maze.elpk", "missing");
    const bool cancelled_read = vfs.cancel(cancelled) && vfs.state(cancelled) == VirtualReadState::Cancelled;
    const VirtualReadHandle stale = vfs.request("maze.elpk", "mesh");
    const bool remounted = vfs.mount(base_root, {}, 8);
    const VirtualReadHandle stale_dependency = vfs.request("maze.elpk", "mesh", 6);
    const bool stale_read = remounted && vfs.pump(2) == 2 &&
        vfs.state(stale) == VirtualReadState::Failed &&
        vfs.state(stale_dependency) == VirtualReadState::Failed &&
        vfs.error(stale_dependency) == "stale dependency generation";
    NativeResourceLoader loader(device);
    const bool loader_mounted = loader.mount(base_root, {override_root}, 9);
    const NativeAssetHandle asset = loader.request("maze.elpk", "mesh");
    const NativeAssetHandle duplicate_asset = loader.request("maze.elpk", "mesh");
    auto loader_worker = loader.pump_io_async(1);
    const bool loader_worker_done = loader_worker.get() == 1;
    const bool asset_loaded = loader_mounted && asset.slot == duplicate_asset.slot &&
        loader_worker_done && loader.upload_ready(1) == 1 &&
        loader.state(asset) == NativeAssetState::Resident &&
        loader.texture(asset) != nullptr && loader.telemetry().coalesced == 1;
    const bool asset_released = loader.release(asset) &&
        loader.state(asset) == NativeAssetState::Empty && loader.texture(asset) == nullptr &&
        loader.telemetry().released == 1;
    const NativeAssetHandle reused_asset = loader.request("maze.elpk", "mesh");
    auto reused_worker = loader.pump_io_async(1);
    const bool asset_slot_reused = reused_asset.slot == asset.slot &&
        reused_asset.generation != asset.generation && reused_worker.get() == 1 &&
        loader.upload_ready(1) == 1 && loader.state(reused_asset) == NativeAssetState::Resident;
    const NativeAssetHandle cancelled_asset = loader.request("maze.elpk", "missing");
    const bool asset_cancelled = loader.cancel(cancelled_asset) &&
        loader.state(cancelled_asset) == NativeAssetState::Cancelled;
    const NativeAssetHandle retried_cancelled_asset = loader.request("maze.elpk", "missing");
    const bool cancelled_slot_reused = retried_cancelled_asset.slot == cancelled_asset.slot &&
        retried_cancelled_asset.generation != cancelled_asset.generation &&
        loader.cancel(retried_cancelled_asset);
    const NativeAssetHandle ready_cancel_asset = loader.request("dep.elpk", "mesh");
    auto ready_cancel_worker = loader.pump_io_async(1);
    const bool ready_cancelled = ready_cancel_worker.get() == 1 &&
        loader.state(ready_cancel_asset) == NativeAssetState::Queued &&
        loader.cancel(ready_cancel_asset) && loader.state(ready_cancel_asset) == NativeAssetState::Cancelled;
    const NativeAssetHandle corrupt_asset = loader.request("corrupt.elpk", "mesh");
    auto corrupt_worker = loader.pump_io_async(1);
    const bool corrupt_worker_done = corrupt_worker.get() == 1;
    const uint32_t uploads_before_corrupt = loader.telemetry().uploaded;
    const uint32_t corrupt_uploads = loader.upload_ready(1);
    const bool corrupt_asset_rejected = corrupt_worker_done && corrupt_uploads == 0 &&
        loader.state(corrupt_asset) == NativeAssetState::Failed &&
        loader.texture(corrupt_asset) == nullptr &&
        loader.telemetry().uploaded == uploads_before_corrupt;
    const NativeAssetHandle retried_failed_asset = loader.request("corrupt.elpk", "mesh");
    auto retry_worker = loader.pump_io_async(1);
    const bool failed_slot_reused = retried_failed_asset.slot == corrupt_asset.slot &&
        retried_failed_asset.generation != corrupt_asset.generation && retry_worker.get() == 1 &&
        loader.upload_ready(1) == 0 && loader.state(retried_failed_asset) == NativeAssetState::Failed;
    const NativeAssetHandle stale_asset = loader.request("maze.elpk", "mesh", 8);
    const bool asset_stale = loader.pump(1, 1) == 0 && loader.state(stale_asset) == NativeAssetState::Failed;
    const bool result = check(load_cooked_package(valid_package).loaded, "bounded package load") &&
        check(uv_package.loaded && uv_package.uv_data.size() == 6,
            "cooked FBX UV channel survives native package loading") &&
        check(runtime_geometry_loaded && runtime_geometry.positions.size() == 9 &&
            runtime_geometry.normals.size() == 9 && runtime_geometry.uvs.size() == 6 &&
            runtime_geometry.indices.size() == 3 && runtime_geometry.indices[2] == 2,
            "Elisa runtime cooked-mesh reader validates and decodes geometry") &&
        check(!load_cooked_package(duplicate.string()).loaded, "duplicate package section rejected") &&
        check(!load_cooked_package(traversal.string()).loaded, "package traversal rejected") &&
        check(!load_cooked_package(malformed.string()).loaded, "malformed package rejected") &&
        check(package_crc32(reinterpret_cast<const uint8_t*>("123456789"), 9) == 0xCBF43926u,
            "CRC-32 standard check value") &&
        check(unsorted_manifest_rejected, "unsorted package manifest rejected") &&
        check(read_binary_package_index(binary.string()).valid, "binary package index") &&
        check(!read_binary_package_index(overlap.string()).valid, "binary package overlap rejected") &&
        check(!read_binary_package_index(compressed.string()).valid, "binary package compression rejected") &&
        check(read_binary_package_section(zstd.string(), zstd_index, "mesh", section, section_error) &&
            std::string(section.begin(), section.end()) == "elisa-bundle-section", "zstd binary section read") &&
        check(corrupt_index.valid && !read_binary_package_section(corrupt.string(), corrupt_index,
            "mesh", corrupt_section, corrupt_error) && corrupt_section.empty() &&
            corrupt_error == "binary section checksum mismatch", "corrupt binary section rejected") &&
        check(resolve_package_path(base_root, {override_root}, "maze.elpk", 7).found &&
            resolve_package_path(base_root, {override_root}, "maze.elpk", 7).override_used &&
            resolve_package_path(base_root, {override_root}, "maze.elpk", 7).generation == 7, "package override resolution") &&
        check(worker_read_ok, "virtual file worker scheduling") &&
        check(dependency_rejections, "virtual file worker dependency validation") &&
        check(cancelled_in_flight, "virtual file in-flight cancellation does not block publication") &&
        check(worker_shutdown_waits, "virtual file worker lifetime joined on service destruction") &&
        check(remount_epoch_invalidates, "virtual file remount epoch invalidation") &&
        check(dependency_graph, "package manifest dependency DAG order and cycle rejection") &&
        check(chain_of_sixteen && chain_of_seventeen_rejected, "package dependency chain bounded at 16") &&
        check(nested_names_resolve, "package dependency names relative to their package") &&
        check(bombs_rejected, "zstd decompression bombs rejected by declared size") &&
        check(resolve_package_path(base_root, {}, "maze.elpk", 8).found &&
            !resolve_package_path(base_root, {}, "maze.elpk", 8).override_used, "package base resolution") &&
        check(!resolve_package_path(base_root, {}, "../maze.elpk", 8).found, "package override traversal rejected") &&
        check(!safe_package_path("models/./wall.elpk"), "package dot segment rejected") &&
        symlink_escape_ok &&
        check(!resolve_package_path(base_root, {}, "maze.elpk", 0).found, "package zero generation rejected") &&
        check(virtual_read, "virtual file read coalescing") &&
        check(cancelled_read, "virtual file cancellation") &&
        check(stale_read, "virtual file generation invalidation");
    const bool loader_result = check(asset_loaded, "native loader coalesced upload") &&
        check(asset_released && asset_slot_reused, "native loader resident release and slot reuse") &&
        check(asset_cancelled && cancelled_slot_reused, "native loader cancellation and slot reuse") &&
        check(ready_cancelled, "native loader drains completed read on cancellation") &&
        check(corrupt_asset_rejected, "corrupt section rejected before GPU upload") &&
        check(failed_slot_reused, "native loader failed slot retry") &&
        check(asset_stale, "native loader dependency generation");
    std::filesystem::remove_all(root);
    return result && loader_result;
}

} // namespace probe
