#pragma once
// Support code for the native scene probe: fixture parsing, the
// right-handed to Wicked-space conversion, cell markers, and a generated
// test WAV. Split out of native/wicked_probe.cpp so the probe stays under
// the 600-line file limit that the engine applies to every source file.
#include "wiScene.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace probe {
bool check(bool value, const char* message) {
    if (!value) {
        std::fprintf(stderr, "wicked probe failed: %s\n", message);
        return false;
    }
    return true;
}

bool load_manifest(const char* filename, std::map<std::string, std::string>& values) {
    std::ifstream input(filename);
    if (!input) {
        return false;
    }
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const auto separator = line.find('=');
        if (separator == std::string::npos) {
            return false;
        }
        values[line.substr(0, separator)] = line.substr(separator + 1);
    }
    return true;
}

// Elisa math is right-handed with +Y up and -Z forward (see
// src/math/geometry.elisa), and so is the Godot host. Wicked is a
// left-handed renderer, so its screen-right is Elisa's +X. Negating X at
// this boundary is the RH->LH conversion that keeps both hosts showing the
// same world instead of a mirrored one. Verified by sampling projected
// maze cells in both frames: without this the native image is the mirror
// of the Godot image.
XMFLOAT3 to_wicked_space(float x, float y, float z) {
    return XMFLOAT3(-x, y, z);
}

// "x,y;x,y;..." grid cells from the Elisa maze topology. Empty when the
// manifest has no wall line, which keeps single-cube hosts working.
std::vector<std::pair<int, int>> parse_walls(const std::string& spec) {
    std::vector<std::pair<int, int>> cells;
    size_t start = 0;
    while (start < spec.size()) {
        size_t end = spec.find(';', start);
        if (end == std::string::npos) {
            end = spec.size();
        }
        const std::string entry = spec.substr(start, end - start);
        const auto comma = entry.find(',');
        if (comma == std::string::npos) {
            return {};
        }
        cells.emplace_back(std::stoi(entry.substr(0, comma)), std::stoi(entry.substr(comma + 1)));
        start = end + 1;
    }
    return cells;
}

// A small unlit cube marking one game cell: player-facing evidence that the
// host draws the Elisa game's objects (key, door, hazards, goal), not just
// walls. Same RH->LH conversion as every other position.
wi::ecs::Entity create_cell_marker(wi::scene::Scene& scene, const std::string& name,
    int cell_x, int cell_y, float red, float green, float blue) {
    const auto entity = scene.Entity_CreateCube(name);
    if (entity == wi::ecs::INVALID_ENTITY) {
        return entity;
    }
    auto* transform = scene.transforms.GetComponent(entity);
    auto* material = scene.materials.GetComponent(entity);
    if (transform == nullptr) {
        return wi::ecs::INVALID_ENTITY;
    }
    transform->translation_local = to_wicked_space(
        (float)cell_x * 0.6f - 2.1f, (float)cell_y * 0.6f - 2.1f, 1.0f);
    transform->scale_local = XMFLOAT3(0.26f, 0.26f, 0.26f);
    transform->UpdateTransform();
    if (material != nullptr) {
        material->shaderType = wi::scene::MaterialComponent::SHADERTYPE_UNLIT;
        material->baseColor = XMFLOAT4(red, green, blue, 1.0f);
    }
    return entity;
}

// A tiny PCM WAV in memory: 16-bit mono at 8 kHz for 50 ms. No asset file is
// needed to prove the audio path decodes bytes and reports real sample
// information, which is stronger evidence than "a play call returned".
std::vector<uint8_t> make_test_wav(int sample_count, int sample_rate) {
    const int data_bytes = sample_count * 2;
    const int file_bytes = 44 + data_bytes;
    std::vector<uint8_t> wav(file_bytes, 0);
    auto put16 = [&wav](int offset, int value) {
        wav[offset] = (uint8_t)(value & 0xFF);
        wav[offset + 1] = (uint8_t)((value >> 8) & 0xFF);
    };
    auto put32 = [&wav](int offset, int value) {
        wav[offset] = (uint8_t)(value & 0xFF);
        wav[offset + 1] = (uint8_t)((value >> 8) & 0xFF);
        wav[offset + 2] = (uint8_t)((value >> 16) & 0xFF);
        wav[offset + 3] = (uint8_t)((value >> 24) & 0xFF);
    };
    wav[0] = 'R'; wav[1] = 'I'; wav[2] = 'F'; wav[3] = 'F';
    put32(4, file_bytes - 8);
    wav[8] = 'W'; wav[9] = 'A'; wav[10] = 'V'; wav[11] = 'E';
    wav[12] = 'f'; wav[13] = 'm'; wav[14] = 't'; wav[15] = ' ';
    put32(16, 16);
    put16(20, 1);
    put16(22, 1);
    put32(24, sample_rate);
    put32(28, sample_rate * 2);
    put16(32, 2);
    put16(34, 16);
    wav[36] = 'd'; wav[37] = 'a'; wav[38] = 't'; wav[39] = 'a';
    put32(40, data_bytes);
    for (int i = 0; i < sample_count; ++i) {
        const int sample = (i % 32 < 16) ? 4000 : -4000;
        put16(44 + i * 2, sample & 0xFFFF);
    }
    return wav;
}
}
