#include "mikktspace_geometry.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <limits>
#include <vector>

namespace {

constexpr uint32_t MAX_VERTICES = 2'000'000;
constexpr uint32_t MAX_INDICES = 15'000'000;
constexpr char INPUT_MAGIC[8] = {'E', 'L', 'I', 'S', 'A', 'M', 'K', '1'};
constexpr char OUTPUT_MAGIC[8] = {'E', 'L', 'I', 'S', 'A', 'M', 'R', '1'};

template <typename T>
bool read_values(std::istream& input, std::vector<T>& values) {
    if (values.empty()) return true;
    const size_t bytes = values.size() * sizeof(T);
    if (bytes > size_t(std::numeric_limits<std::streamsize>::max())) return false;
    input.read(reinterpret_cast<char*>(values.data()), std::streamsize(bytes));
    return input.good();
}

template <typename T>
bool write_values(std::ostream& output, const std::vector<T>& values) {
    if (values.empty()) return true;
    const size_t bytes = values.size() * sizeof(T);
    if (bytes > size_t(std::numeric_limits<std::streamsize>::max())) return false;
    output.write(reinterpret_cast<const char*>(values.data()), std::streamsize(bytes));
    return output.good();
}

} // namespace

int main() {
    char magic[8]{};
    uint32_t vertex_count = 0, index_count = 0;
    std::cin.read(magic, sizeof(magic));
    std::cin.read(reinterpret_cast<char*>(&vertex_count), sizeof(vertex_count));
    std::cin.read(reinterpret_cast<char*>(&index_count), sizeof(index_count));
    if (!std::cin.good() || !std::equal(std::begin(magic), std::end(magic), std::begin(INPUT_MAGIC)) ||
        vertex_count == 0 || vertex_count > MAX_VERTICES || index_count == 0 ||
        index_count > MAX_INDICES || index_count % 3 != 0) {
        std::cerr << "invalid bounded MikkTSpace input header\n";
        return 1;
    }

    std::vector<float> positions(size_t(vertex_count) * 3);
    std::vector<float> normals(size_t(vertex_count) * 3);
    std::vector<float> uvs(size_t(vertex_count) * 2);
    std::vector<uint32_t> indices(index_count);
    if (!read_values(std::cin, positions) || !read_values(std::cin, normals) ||
        !read_values(std::cin, uvs) || !read_values(std::cin, indices) ||
        std::cin.peek() != std::char_traits<char>::eof()) {
        std::cerr << "truncated or trailing MikkTSpace input\n";
        return 1;
    }

    elisa::assets::MikkGeometry geometry;
    if (!elisa::assets::generate_mikktspace_geometry(positions, normals, uvs, indices, geometry,
            MAX_VERTICES) ||
        geometry.source_vertices.empty() || geometry.source_vertices.size() > MAX_VERTICES ||
        geometry.indices.size() != indices.size()) {
        std::cerr << "MikkTSpace rejected the bounded geometry\n";
        return 1;
    }
    const uint32_t output_vertex_count = uint32_t(geometry.source_vertices.size());
    const uint32_t output_index_count = uint32_t(geometry.indices.size());
    std::cout.write(OUTPUT_MAGIC, sizeof(OUTPUT_MAGIC));
    std::cout.write(reinterpret_cast<const char*>(&output_vertex_count), sizeof(output_vertex_count));
    std::cout.write(reinterpret_cast<const char*>(&output_index_count), sizeof(output_index_count));
    if (!std::cout.good() || !write_values(std::cout, geometry.positions) ||
        !write_values(std::cout, geometry.normals) || !write_values(std::cout, geometry.uvs) ||
        !write_values(std::cout, geometry.tangents) || !write_values(std::cout, geometry.indices) ||
        !write_values(std::cout, geometry.source_vertices)) {
        std::cerr << "failed to write MikkTSpace output\n";
        return 1;
    }
    return 0;
}
