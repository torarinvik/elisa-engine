#pragma once
// Native skinned-mesh submission: build a quad mesh from the engine-deformed
// positions the fixture carries, upload it, verify it, and release it. The
// native host does no skinning; it only moves the vertices Elisa computed.
#include "probe_core.h"
#include "probe_support.h"
#include "coordinate_fixture.h"
#include "coordinate_transform_bridge.h"
#include "package_load.h"

#include <cstdio>
#include <cmath>
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
    constexpr float TRANSLATION_X = 0.25f;
    constexpr float TRANSLATION_Y = -0.5f;
    constexpr float TRANSLATION_Z = 0.75f;
    constexpr float SCALE_X = -2.0f;
    constexpr float SCALE_Y = 0.5f;
    constexpr float SCALE_Z = 1.5f;
    constexpr float POSITION_TOLERANCE = 0.001f;
    const XMFLOAT3 authored_scale(SCALE_X, SCALE_Y, SCALE_Z);
    CookedPackage quad;
    quad.loaded = true;
    quad.format = "elisa-skin";
    for (const auto& point : points) {
        const XMFLOAT3 wicked_point = coordinates::to_wicked(XMFLOAT3(point.first, point.second, 0.0f));
        quad.position_data.push_back(wicked_point.x);
        quad.position_data.push_back(wicked_point.y);
        quad.position_data.push_back(wicked_point.z);
        quad.normal_data.push_back(0.0f);
        quad.normal_data.push_back(0.0f);
        quad.normal_data.push_back(1.0f);
    }
    quad.index_data = { 0u, 1u, 2u, 0u, 2u, 3u };
    const bool reverse_winding = coordinates::winding_reversed(authored_scale);
    if (!check(!reverse_winding, "negative skin scale cancels reflected-basis winding")) return false;
    if (reverse_winding) {
        for (size_t index = 0; index < quad.index_data.size(); index += 3) {
            std::swap(quad.index_data[index + 1], quad.index_data[index + 2]);
        }
    }
    const size_t objects_before = scene.objects.GetCount();
    const auto entity = create_cooked_mesh(scene, "elisa_skin_probe", quad,
        to_wicked_space(0.0f, 0.0f, 0.0f), 1.0f, XMFLOAT4(0.4f, 0.9f, 0.4f, 1.0f));
    if (!check(entity != wi::ecs::INVALID_ENTITY, "skinned quad mesh built")) {
        return false;
    }
    auto* mesh = scene.meshes.GetComponent(entity);
    auto* transform = scene.transforms.GetComponent(entity);
    const bool uploaded = mesh != nullptr && transform != nullptr &&
        mesh->vertex_positions.size() == 4 && mesh->indices.size() == 6;
    if (!check(uploaded, "skinned quad upload matches the fixture")) {
        return false;
    }
    const ElisaCoordinateProfile profile = elisa_coordinate_profile();
    const ElisaTransformPayload signed_transform{{TRANSLATION_X, TRANSLATION_Y, TRANSLATION_Z},
        {0.0f, 0.0f, 0.0f, 1.0f}, {SCALE_X, SCALE_Y, SCALE_Z}};
    if (!check(submit_elisa_transform(&profile, &signed_transform, transform),
        "signed nonuniform skin-mesh transform submission")) return false;
    transform->UpdateTransform();
    for (size_t index = 0; index < points.size(); ++index) {
        const XMFLOAT3 local = mesh->vertex_positions[index];
        XMFLOAT3 actual{};
        XMStoreFloat3(&actual, XMVector3TransformCoord(XMLoadFloat3(&local), XMLoadFloat4x4(&transform->world)));
        const float expected_x = -(TRANSLATION_X + SCALE_X * points[index].first);
        const float expected_y = TRANSLATION_Y + SCALE_Y * points[index].second;
        const float expected_z = TRANSLATION_Z;
        if (!check(std::isfinite(actual.x) && std::isfinite(actual.y) && std::isfinite(actual.z) &&
            std::fabs(actual.x - expected_x) <= POSITION_TOLERANCE &&
            std::fabs(actual.y - expected_y) <= POSITION_TOLERANCE &&
            std::fabs(actual.z - expected_z) <= POSITION_TOLERANCE,
            "signed-scale skin vertex reaches expected Wicked world position")) return false;
    }
    std::fprintf(stdout, "skinned quad: vertices=%u indices=%u signed_scale=(-2.000,0.500,1.500) first_world=(%.3f,%.3f,%.3f)\n",
        (unsigned)mesh->vertex_positions.size(), (unsigned)mesh->indices.size(),
        - (TRANSLATION_X + SCALE_X * points[0].first),
        TRANSLATION_Y + SCALE_Y * points[0].second, TRANSLATION_Z);
    scene.Entity_Remove(entity);
    return check(scene.objects.GetCount() == objects_before, "skinned quad released");
}

} // namespace probe
