#include "fbx_asset_import.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace {

constexpr size_t MAX_PACKAGE_BYTES = size_t(64) * 1024 * 1024;
constexpr size_t MAX_LINE_BYTES = size_t(16) * 1024 * 1024;

bool safe_asset_key(const std::string& value) {
    if (value.empty() || value.size() > 4096 || value.front() == '/' ||
        value.find('\\') != std::string::npos || value.find_first_of("\r\n") != std::string::npos ||
        value.find('\0') != std::string::npos) return false;
    size_t start = 0;
    while (start < value.size()) {
        const size_t end = value.find('/', start);
        const std::string component = value.substr(start,
            end == std::string::npos ? std::string::npos : end - start);
        if (component.empty() || component == "." || component == "..") return false;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return true;
}

void append_u32_le(std::vector<uint8_t>& bytes, uint32_t value) {
    bytes.push_back(uint8_t(value));
    bytes.push_back(uint8_t(value >> 8));
    bytes.push_back(uint8_t(value >> 16));
    bytes.push_back(uint8_t(value >> 24));
}

std::vector<uint8_t> float_bytes(const std::vector<float>& values) {
    std::vector<uint8_t> bytes;
    bytes.reserve(values.size() * sizeof(float));
    for (float value : values) {
        uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        append_u32_le(bytes, bits);
    }
    return bytes;
}

std::vector<uint8_t> index_bytes(const std::vector<uint32_t>& values) {
    std::vector<uint8_t> bytes;
    bytes.reserve(values.size() * sizeof(uint32_t));
    for (uint32_t value : values) append_u32_le(bytes, value);
    return bytes;
}

size_t base64_size(size_t byte_count) {
    if (byte_count > std::numeric_limits<size_t>::max() - 2) return std::numeric_limits<size_t>::max();
    const size_t groups = (byte_count + 2) / 3;
    if (groups > std::numeric_limits<size_t>::max() / 4) return std::numeric_limits<size_t>::max();
    return groups * 4;
}

std::string base64(const std::vector<uint8_t>& bytes) {
    static constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string encoded;
    encoded.reserve(base64_size(bytes.size()));
    for (size_t index = 0; index < bytes.size(); index += 3) {
        const uint32_t first = bytes[index];
        const uint32_t second = index + 1 < bytes.size() ? bytes[index + 1] : 0;
        const uint32_t third = index + 2 < bytes.size() ? bytes[index + 2] : 0;
        const uint32_t word = (first << 16) | (second << 8) | third;
        encoded.push_back(alphabet[(word >> 18) & 63]);
        encoded.push_back(alphabet[(word >> 12) & 63]);
        encoded.push_back(index + 1 < bytes.size() ? alphabet[(word >> 6) & 63] : '=');
        encoded.push_back(index + 2 < bytes.size() ? alphabet[word & 63] : '=');
    }
    return encoded;
}

bool cook(const std::filesystem::path& source, const std::string& asset_key,
    const std::filesystem::path& output, const std::string& source_sha256) {
    if (!safe_asset_key(asset_key)) {
        std::fprintf(stderr, "unsafe asset key; use a project-relative path without `..`\n");
        return false;
    }
    if (source_sha256.size() != 64 || source_sha256.find_first_not_of("0123456789abcdef") != std::string::npos) {
        std::fprintf(stderr, "source SHA-256 must be 64 lowercase hexadecimal characters\n");
        return false;
    }
    const elisa::assets::FbxImportResult asset = elisa::assets::import_fbx(source, true);
    if (!asset.ok) {
        std::fprintf(stderr, "FBX cook failed: %s\n", asset.error.c_str());
        return false;
    }
    const auto& mesh = asset.primary_mesh;
    if (mesh.positions.empty() || mesh.positions.size() % 3 != 0 ||
        mesh.normals.size() != mesh.positions.size() || mesh.uvs.size() != mesh.positions.size() / 3 * 2 ||
        mesh.indices.empty() || mesh.indices.size() % 3 != 0) {
        std::fprintf(stderr, "FBX importer returned incomplete triangle geometry\n");
        return false;
    }
    for (uint32_t index : mesh.indices) {
        if (index >= mesh.positions.size() / 3) {
            std::fprintf(stderr, "FBX importer returned an out-of-range mesh index\n");
            return false;
        }
    }
    const std::vector<uint8_t> positions = float_bytes(mesh.positions);
    const std::vector<uint8_t> normals = float_bytes(mesh.normals);
    const std::vector<uint8_t> uvs = float_bytes(mesh.uvs);
    const std::vector<uint8_t> indices = index_bytes(mesh.indices);
    const size_t positions_encoded = base64_size(positions.size());
    const size_t normals_encoded = base64_size(normals.size());
    const size_t uvs_encoded = base64_size(uvs.size());
    const size_t indices_encoded = base64_size(indices.size());
    const size_t max_payload = MAX_LINE_BYTES - 14;
    if (positions_encoded > max_payload || normals_encoded > max_payload ||
        uvs_encoded > max_payload || indices_encoded > max_payload) {
        std::fprintf(stderr, "FBX geometry exceeds the cooked package line limit; simplify or split the source asset\n");
        return false;
    }
    const size_t metadata_budget = 2048 + asset_key.size();
    if (positions_encoded > MAX_PACKAGE_BYTES - metadata_budget ||
        normals_encoded > MAX_PACKAGE_BYTES - metadata_budget - positions_encoded ||
        uvs_encoded > MAX_PACKAGE_BYTES - metadata_budget - positions_encoded - normals_encoded ||
        indices_encoded > MAX_PACKAGE_BYTES - metadata_budget - positions_encoded - normals_encoded - uvs_encoded) {
        std::fprintf(stderr, "FBX geometry exceeds the cooked package size limit; simplify or split the source asset\n");
        return false;
    }

    std::ostringstream package;
    package.imbue(std::locale::classic());
    package << std::setprecision(std::numeric_limits<float>::max_digits10);
    package << "format=elisa-cooked-v2\n"
        << "source=" << asset_key << "\n"
        << "source_sha256=" << source_sha256 << "\n"
        << "triangles=" << mesh.indices.size() / 3 << "\n"
        << "positions=" << mesh.positions.size() / 3 << "\n"
        << "indices=" << mesh.indices.size() << "\n"
        << "bounds_min=" << mesh.bounds_min[0] << ',' << mesh.bounds_min[1] << ',' << mesh.bounds_min[2] << "\n"
        << "bounds_max=" << mesh.bounds_max[0] << ',' << mesh.bounds_max[1] << ',' << mesh.bounds_max[2] << "\n"
        << "position_stride=12\nnormal_stride=12\nuv_stride=8\nindex_stride=4\n"
        << "positions_b64=" << base64(positions) << "\n"
        << "normals_b64=" << base64(normals) << "\n"
        << "uvs_b64=" << base64(uvs) << "\n"
        << "indices_b64=" << base64(indices) << "\n";
    const std::string bytes = package.str();
    if (bytes.size() > MAX_PACKAGE_BYTES) {
        std::fprintf(stderr, "FBX geometry exceeds the cooked package size limit\n");
        return false;
    }

    std::error_code filesystem_error;
    std::filesystem::create_directories(output.parent_path(), filesystem_error);
    if (filesystem_error) {
        std::fprintf(stderr, "cannot create package output directory: %s\n", filesystem_error.message().c_str());
        return false;
    }
    std::filesystem::path temporary = output;
    temporary += ".tmp";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) {
            std::fprintf(stderr, "cannot open temporary package output\n");
            return false;
        }
        stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!stream) {
            stream.close();
            std::filesystem::remove(temporary, filesystem_error);
            std::fprintf(stderr, "failed while writing cooked package\n");
            return false;
        }
    }
    std::filesystem::rename(temporary, output, filesystem_error);
    if (filesystem_error) {
        std::filesystem::remove(temporary);
        std::fprintf(stderr, "cannot publish cooked package: %s\n", filesystem_error.message().c_str());
        return false;
    }
    std::printf("cooked %s -> %s (%zu triangles, %zu vertices, %zu bytes)\n",
        asset_key.c_str(), output.string().c_str(), mesh.indices.size() / 3,
        mesh.positions.size() / 3, bytes.size());
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 9 || std::string(argv[1]) != "--source" ||
        std::string(argv[3]) != "--asset-path" || std::string(argv[5]) != "--output" ||
        std::string(argv[7]) != "--sha256") {
        std::fprintf(stderr, "usage: fbx_asset_cooker --source FILE --asset-path PROJECT_RELATIVE_PATH --output FILE --sha256 HEX\n");
        return 2;
    }
    return cook(argv[2], argv[4], argv[6], argv[8]) ? 0 : 1;
}
