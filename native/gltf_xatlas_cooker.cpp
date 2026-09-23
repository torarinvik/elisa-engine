#include "xatlas.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
constexpr char INPUT_MAGIC[8] = {'E', 'L', 'I', 'S', 'A', 'X', 'A', '1'};
constexpr char OUTPUT_MAGIC[8] = {'E', 'L', 'I', 'S', 'A', 'X', 'R', '1'};
constexpr uint32_t MAX_VERTICES = 2'000'000;
constexpr uint32_t MAX_INDICES = 15'000'000;
constexpr uint32_t MIN_RESOLUTION = 16;
constexpr uint32_t MAX_RESOLUTION = 8192;
constexpr uint32_t MAX_PADDING = 64;

bool read_bytes(void* output, size_t size) {
    return std::fread(output, 1, size, stdin) == size;
}

bool write_bytes(const void* data, size_t size) {
    return std::fwrite(data, 1, size, stdout) == size;
}
}

int main() {
    char magic[8]{};
    uint32_t vertex_count = 0;
    uint32_t index_count = 0;
    uint32_t resolution = 0;
    uint32_t padding = 0;
    if (!read_bytes(magic, sizeof(magic)) || std::memcmp(magic, INPUT_MAGIC, sizeof(magic)) != 0 ||
        !read_bytes(&vertex_count, sizeof(vertex_count)) || !read_bytes(&index_count, sizeof(index_count)) ||
        !read_bytes(&resolution, sizeof(resolution)) || !read_bytes(&padding, sizeof(padding)) ||
        vertex_count == 0 || vertex_count > MAX_VERTICES || index_count < 3 ||
        index_count > MAX_INDICES || index_count % 3 != 0 || resolution < MIN_RESOLUTION ||
        resolution > MAX_RESOLUTION || padding > MAX_PADDING) {
        std::fprintf(stderr, "xatlas input is outside the bounded mesh profile\n");
        return 2;
    }

    std::vector<float> positions(size_t(vertex_count) * 3);
    std::vector<uint32_t> indices(index_count);
    if (!read_bytes(positions.data(), positions.size() * sizeof(float)) ||
        !read_bytes(indices.data(), indices.size() * sizeof(uint32_t))) {
        std::fprintf(stderr, "xatlas input streams are truncated\n");
        return 2;
    }
    for (float value : positions) {
        if (!std::isfinite(value)) {
            std::fprintf(stderr, "xatlas positions contain a non-finite value\n");
            return 2;
        }
    }
    for (uint32_t index : indices) {
        if (index >= vertex_count) {
            std::fprintf(stderr, "xatlas index is out of range\n");
            return 2;
        }
    }

    xatlas::Atlas* atlas = xatlas::Create();
    if (atlas == nullptr) return 3;
    xatlas::MeshDecl declaration;
    declaration.vertexPositionData = positions.data();
    declaration.vertexPositionStride = sizeof(float) * 3;
    declaration.vertexCount = vertex_count;
    declaration.indexData = indices.data();
    declaration.indexCount = index_count;
    declaration.indexFormat = xatlas::IndexFormat::UInt32;
    const xatlas::AddMeshError added = xatlas::AddMesh(atlas, declaration);
    if (added != xatlas::AddMeshError::Success) {
        std::fprintf(stderr, "xatlas rejected mesh: %s\n", xatlas::StringForEnum(added));
        xatlas::Destroy(atlas);
        return 3;
    }
    xatlas::ChartOptions chart_options;
    chart_options.maxIterations = 1;
    xatlas::ComputeCharts(atlas, chart_options);
    xatlas::PackOptions pack_options;
    pack_options.resolution = resolution;
    pack_options.padding = padding;
    pack_options.bilinear = true;
    pack_options.blockAlign = false;
    pack_options.bruteForce = false;
    pack_options.rotateCharts = true;
    xatlas::PackCharts(atlas, pack_options);

    const xatlas::Mesh* mesh = atlas->meshCount == 1 ? &atlas->meshes[0] : nullptr;
    if (mesh == nullptr || atlas->width == 0 || atlas->height == 0 ||
        atlas->width > resolution || atlas->height > resolution || mesh->vertexCount == 0 ||
        mesh->vertexCount > MAX_VERTICES || mesh->indexCount != index_count ||
        mesh->chartCount == 0 || mesh->vertexArray == nullptr || mesh->indexArray == nullptr) {
        std::fprintf(stderr, "xatlas produced an invalid or empty atlas\n");
        xatlas::Destroy(atlas);
        return 3;
    }
    const uint32_t counts[5] = {mesh->vertexCount, mesh->indexCount, mesh->chartCount,
        atlas->width, atlas->height};
    if (!write_bytes(OUTPUT_MAGIC, sizeof(OUTPUT_MAGIC)) || !write_bytes(counts, sizeof(counts))) {
        xatlas::Destroy(atlas);
        return 4;
    }
    for (uint32_t vertex = 0; vertex < mesh->vertexCount; ++vertex) {
        const auto& result = mesh->vertexArray[vertex];
        const float uv[2] = {result.uv[0] / float(atlas->width), result.uv[1] / float(atlas->height)};
        if (result.atlasIndex < 0 || result.xref >= vertex_count ||
            !std::isfinite(uv[0]) || !std::isfinite(uv[1]) || uv[0] < 0.0f || uv[0] > 1.0f ||
            uv[1] < 0.0f || uv[1] > 1.0f ||
            !write_bytes(uv, sizeof(uv)) || !write_bytes(&result.xref, sizeof(result.xref))) {
            std::fprintf(stderr, "xatlas returned an invalid UV vertex\n");
            xatlas::Destroy(atlas);
            return 4;
        }
    }
    for (uint32_t index = 0; index < mesh->indexCount; ++index) {
        if (mesh->indexArray[index] >= mesh->vertexCount ||
            !write_bytes(&mesh->indexArray[index], sizeof(uint32_t))) {
            std::fprintf(stderr, "xatlas returned an invalid index\n");
            xatlas::Destroy(atlas);
            return 4;
        }
    }
    xatlas::Destroy(atlas);
    return 0;
}
