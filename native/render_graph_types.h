#pragma once

#include <array>
#include <cstdint>
#include <limits>

namespace elisa::render_graph {

constexpr uint32_t MAX_RESOURCES = 64;
constexpr uint32_t MAX_PASSES = 32;
constexpr uint32_t MAX_TARGETS = 64;
constexpr uint32_t NO_TARGET = std::numeric_limits<uint32_t>::max();
constexpr float MIN_BLEND_OPACITY = 0.0f;
constexpr float MAX_BLEND_OPACITY = 1.0f;
constexpr float DEFAULT_BLEND_OPACITY = 1.0f;

enum class SizeMode : int32_t { Fixed = 0, PrimaryInternal = 1 };
enum class Format : int32_t { Rgba8 = 0, Rgba16Float = 1, Depth32 = 2, R11G11B10Float = 3, R32Float = 4 };
enum class Lifetime : int32_t { Imported = 0, Persistent = 1, Transient = 2 };
enum class ImportSource : int32_t { None = 0, SceneColor = 1, LinearDepth = 2 };
enum class Operation : int32_t {
    ClearColor = 0, CopyColor = 1, ClearDepth = 2, ResolveColor = 3,
    VisualizeLinearDepth = 4, BlendColor = 5
};

struct Resource {
    uint32_t id = 0;
    SizeMode size_mode = SizeMode::Fixed;
    uint32_t width = 0;
    uint32_t height = 0;
    Format format = Format::Rgba8;
    uint32_t samples = 1;
    Lifetime lifetime = Lifetime::Transient;
    bool initialized = false;
    uint32_t target = NO_TARGET;
    ImportSource import_source = ImportSource::None;
};

struct Pass {
    uint32_t id = 0;
    Operation operation = Operation::ClearColor;
    uint32_t source_id = 0;
    uint32_t destination_id = 0;
    float opacity = DEFAULT_BLEND_OPACITY;
};

struct Configuration {
    std::array<Resource, MAX_RESOURCES> resources{};
    std::array<Pass, MAX_PASSES> passes{};
    std::array<bool, MAX_RESOURCES> resource_set{};
    std::array<bool, MAX_PASSES> pass_set{};
    uint32_t resource_count = 0;
    uint32_t pass_count = 0;
    uint32_t output_id = 0;
    bool building = false;
    bool valid = false;
};

inline bool configuration_valid(const Configuration& config);

} // namespace elisa::render_graph
