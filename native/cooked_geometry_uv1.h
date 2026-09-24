#pragma once

// Parse the optional cooked UV1 stream and its generator provenance.
#include "cooked_package_fields.h"

#include <cstdint>
#include <string>
#include <vector>

namespace elisa::assets::detail {

inline bool parse_geometry_uv1(const probe::PackageIndex& package, uint64_t vertices,
    std::vector<float>& uv1s, std::string& source, uint32_t& resolution, uint32_t& padding,
    uint32_t& charts, std::string& error) {
    const auto stride = package.sections.find("uv1_stride");
    const auto data = package.sections.find("uv1s_b64");
    const auto encoded_data = package.sections.find("uv1s_meshopt_b64");
    const auto source_field = package.sections.find("uv1_source");
    const auto revision = package.sections.find("uv1_generator_revision");
    const auto resolution_field = package.sections.find("uv1_resolution");
    const auto padding_field = package.sections.find("uv1_padding");
    const auto charts_field = package.sections.find("uv1_chart_count");
    const bool has_uv1 = stride != package.sections.end();
    const bool has_xatlas_metadata = revision != package.sections.end() ||
        resolution_field != package.sections.end() || padding_field != package.sections.end() ||
        charts_field != package.sections.end();
    const bool has_uv1_data = data != package.sections.end() || encoded_data != package.sections.end();
    if (has_uv1_data != has_uv1 ||
        (source_field != package.sections.end()) != has_uv1) {
        error = "incomplete cooked geometry UV1 stream";
        return false;
    }
    if (!has_uv1) {
        if (has_xatlas_metadata || source_field != package.sections.end()) {
            error = "UV1 source metadata has no cooked geometry UV1 stream";
            return false;
        }
        return true;
    }
    if (stride->second != "8" ||
        !decode_geometry_floats(package, "uv1s", size_t(vertices), 8, uv1s)) {
        error = "invalid cooked geometry UV1 stream";
        return false;
    }
    source = source_field->second;
    if (source == "xatlas") {
        uint64_t resolution_value = 0;
        uint64_t padding_value = 0;
        uint64_t chart_value = 0;
        if (revision == package.sections.end() ||
            revision->second != "f700c7790aaa030e794b52ba7791a05c085faf0c" ||
            resolution_field == package.sections.end() || padding_field == package.sections.end() ||
            charts_field == package.sections.end() ||
            !parse_count(package, "uv1_resolution", resolution_value) || resolution_value < 16 || resolution_value > 8192 ||
            !parse_count(package, "uv1_padding", padding_value) || padding_value > 64 ||
            !parse_count(package, "uv1_chart_count", chart_value) || chart_value == 0 || chart_value > 2'000'000) {
            error = "invalid cooked geometry UV1 atlas metadata";
            return false;
        }
        resolution = uint32_t(resolution_value);
        padding = uint32_t(padding_value);
        charts = uint32_t(chart_value);
        for (float value : uv1s) {
            if (value < 0.0f || value > 1.0f) {
                error = "cooked geometry UV1 atlas coordinates are outside [0, 1]";
                return false;
            }
        }
    } else if (source != "gltf" || has_xatlas_metadata) {
        error = "unknown cooked geometry UV1 source metadata";
        return false;
    }
    return true;
}

} // namespace elisa::assets::detail
