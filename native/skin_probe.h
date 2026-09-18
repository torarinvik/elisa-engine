#pragma once
// Native skinned-mesh submission: build a quad mesh from the engine-deformed
// positions the fixture carries, upload it, verify it, and release it. The
// native host does no skinning; it only moves the vertices Elisa computed.
#include "probe_core.h"
#include "probe_support.h"
#include "package_load.h"

#include <cstdio>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace probe {

inline std::vector<std::pair<float, float>> parse_point_list(const std::string& spec) {
    std::vector<std::pair<float, float>> points;
    size_t start = 0;
    while (start < spec.size()) {
        size_t end = spec.find(';', start);
        if (end == std::string::npos) {
            end = spec.size();
        }
        const std::string entry = spec.substr(start, end - start);
        const auto comma = entry.find(',');
        if (comma != std::string::npos) {
            points.emplace_back(std::stof(entry.substr(0, comma)), std::stof(entry.substr(comma + 1)));
        }
        start = end + 1;
    }
    return points;
}

inline bool probe_skinned_quad(wi::scene::Scene& scene, const std::map<std::string, std::string>& manifest) {
    const auto quad_it = manifest.find("skin_quad");
    if (quad_it == manifest.end()) {
        return true;
    }
    const auto points = parse_point_list(quad_it->second);
    if (!check(points.size() == 4, "skinned quad has four vertices")) {
        return false;
    }
    CookedPackage quad;
    quad.loaded = true;
    quad.format = "elisa-skin";
    for (const auto& point : points) {
        quad.position_data.push_back(point.first);
        quad.position_data.push_back(point.second);
        quad.position_data.push_back(0.0f);
        quad.normal_data.push_back(0.0f);
        quad.normal_data.push_back(0.0f);
        quad.normal_data.push_back(1.0f);
    }
    quad.index_data = { 0u, 1u, 2u, 0u, 2u, 3u };
    const size_t objects_before = scene.objects.GetCount();
    const auto entity = create_cooked_mesh(scene, "elisa_skin_probe", quad,
        to_wicked_space(60.0f, 0.0f, 0.0f), 0.3f, XMFLOAT4(0.4f, 0.9f, 0.4f, 1.0f));
    if (!check(entity != wi::ecs::INVALID_ENTITY, "skinned quad mesh built")) {
        return false;
    }
    auto* mesh = scene.meshes.GetComponent(entity);
    const bool uploaded = mesh != nullptr && mesh->vertex_positions.size() == 4 && mesh->indices.size() == 6;
    if (!check(uploaded, "skinned quad upload matches the fixture")) {
        return false;
    }
    std::fprintf(stdout, "skinned quad: vertices=%u indices=%u first=(%.3f,%.3f)\n",
        (unsigned)mesh->vertex_positions.size(), (unsigned)mesh->indices.size(),
        mesh->vertex_positions.front().x, mesh->vertex_positions.front().y);
    scene.Entity_Remove(entity);
    return check(scene.objects.GetCount() == objects_before, "skinned quad released");
}

} // namespace probe
