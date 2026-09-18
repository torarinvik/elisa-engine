#pragma once
// Runtime side of the asset pipeline: load a cooked package and expose what
// it carries, including geometry, so a host can build meshes from normalized
// data instead of parsing a source format. Packages are produced offline by
// scripts/cook_assets.py; the runtime only accepts a format it knows.
#include "wiScene.h"
#include "meshopt_probe.h"

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace probe {

struct CookedPackage {
    std::string format;
    long long triangles = -1;
    long long positions = -1;
    long long indices = -1;
    std::vector<float> position_data;
    std::vector<float> normal_data;
    std::vector<uint32_t> index_data;
    bool loaded = false;
};

inline std::vector<uint8_t> decode_base64(const std::string& text) {
    static const std::string alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<uint8_t> out;
    int accumulator = 0;
    int bits = 0;
    for (char character : text) {
        const auto position = alphabet.find(character);
        if (position == std::string::npos) {
            continue;
        }
        accumulator = (accumulator << 6) | (int)position;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back((uint8_t)((accumulator >> bits) & 0xFF));
        }
    }
    return out;
}

inline void decode_floats(const std::string& text, std::vector<float>& out) {
    const std::vector<uint8_t> bytes = decode_base64(text);
    out.resize(bytes.size() / 4);
    for (size_t i = 0; i < out.size(); ++i) {
        uint32_t bits = (uint32_t)bytes[i * 4] | ((uint32_t)bytes[i * 4 + 1] << 8) |
            ((uint32_t)bytes[i * 4 + 2] << 16) | ((uint32_t)bytes[i * 4 + 3] << 24);
        float value = 0.0f;
        std::memcpy(&value, &bits, sizeof(value));
        out[i] = value;
    }
}

inline void decode_u32(const std::string& text, std::vector<uint32_t>& out) {
    const std::vector<uint8_t> bytes = decode_base64(text);
    out.resize(bytes.size() / 4);
    for (size_t i = 0; i < out.size(); ++i) {
        out[i] = (uint32_t)bytes[i * 4] | ((uint32_t)bytes[i * 4 + 1] << 8) |
            ((uint32_t)bytes[i * 4 + 2] << 16) | ((uint32_t)bytes[i * 4 + 3] << 24);
    }
}

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
        } else if (key == "indices") {
            package.indices = std::stoll(value);
        } else if (key == "positions_b64") {
            decode_floats(value, package.position_data);
        } else if (key == "normals_b64") {
            decode_floats(value, package.normal_data);
        } else if (key == "indices_b64") {
            decode_u32(value, package.index_data);
        }
    }
    package.loaded = package.format == "elisa-cooked-v2" &&
        package.position_data.size() == (size_t)package.positions * 3 &&
        package.index_data.size() >= (size_t)package.triangles * 3 &&
        package.normal_data.size() == package.position_data.size();
    return package;
}

// Build a renderable mesh from cooked geometry. The entity is created through
// the engine's cube helper so the object/mesh/material components and subset
// are set up correctly, then the geometry is replaced with the cooked data
// and re-uploaded. Both the cube and the fixture asset are 24-vertex boxes,
// so the normals array stays consistent; a differing asset would need the
// normals cooked alongside (they are, and are copied here when present).
inline wi::ecs::Entity create_cooked_mesh(wi::scene::Scene& scene, const std::string& name,
    const CookedPackage& package, const XMFLOAT3& position, float scale, const XMFLOAT4& color) {
    const auto entity = scene.Entity_CreateCube(name);
    if (entity == wi::ecs::INVALID_ENTITY || !package.loaded) {
        return entity;
    }
    auto* mesh = scene.meshes.GetComponent(entity);
    auto* transform = scene.transforms.GetComponent(entity);
    auto* material = scene.materials.GetComponent(entity);
    if (mesh == nullptr || transform == nullptr) {
        return wi::ecs::INVALID_ENTITY;
    }
    mesh->vertex_positions.resize(package.position_data.size() / 3);
    for (size_t i = 0; i < mesh->vertex_positions.size(); ++i) {
        mesh->vertex_positions[i] = XMFLOAT3(package.position_data[i * 3],
            package.position_data[i * 3 + 1], package.position_data[i * 3 + 2]);
    }
    if (package.normal_data.size() == package.position_data.size()) {
        mesh->vertex_normals.resize(package.normal_data.size() / 3);
        for (size_t i = 0; i < mesh->vertex_normals.size(); ++i) {
            mesh->vertex_normals[i] = XMFLOAT3(package.normal_data[i * 3],
                package.normal_data[i * 3 + 1], package.normal_data[i * 3 + 2]);
        }
    }
    mesh->indices = package.index_data;
    // Cache-optimize the index buffer before upload; the measured ACMR gate
    // lives in native/meshopt_probe.h.
    if (!optimize_vertex_cache(mesh->indices, mesh->vertex_positions.size())) {
        return wi::ecs::INVALID_ENTITY;
    }
    if (!mesh->subsets.empty()) {
        mesh->subsets[0].indexCount = (uint32_t)package.index_data.size();
        mesh->subsets[0].indexOffset = 0;
    }
    mesh->CreateRenderData();
    transform->translation_local = position;
    transform->scale_local = XMFLOAT3(scale, scale, scale);
    transform->SetDirty();
    transform->UpdateTransform();
    if (material != nullptr) {
        material->shaderType = wi::scene::MaterialComponent::SHADERTYPE_UNLIT;
        material->baseColor = color;
    }
    return entity;
}

} // namespace probe
