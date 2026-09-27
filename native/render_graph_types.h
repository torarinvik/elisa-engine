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
constexpr float MIN_SATURATION = 0.0f;
constexpr float MAX_SATURATION = 2.0f;
constexpr float DEFAULT_SATURATION = 1.0f;
constexpr float MIN_COLOR_SCALE = 0.0f;
constexpr float MAX_COLOR_SCALE = 8.0f;
constexpr float DEFAULT_COLOR_SCALE = 1.0f;
constexpr float MIN_TINT_CHANNEL = 0.0f;
constexpr float MAX_TINT_CHANNEL = 8.0f;
constexpr float DEFAULT_TINT_CHANNEL = 1.0f;
constexpr float MIN_TINT_ALPHA = 0.0f;
constexpr float MAX_TINT_ALPHA = 1.0f;
constexpr float DEFAULT_TINT_ALPHA = 1.0f;
constexpr float MIN_SHARPEN_AMOUNT = 0.0f;
constexpr float MAX_SHARPEN_AMOUNT = 1.0f;
constexpr float DEFAULT_SHARPEN_AMOUNT = 0.0f;
constexpr float MIN_CHROMATIC_ABERRATION = 0.0f;
constexpr float MAX_CHROMATIC_ABERRATION = 8.0f;
constexpr float DEFAULT_CHROMATIC_ABERRATION = 0.0f;

enum class SizeMode : int32_t { Fixed = 0, PrimaryInternal = 1 };
enum class Format : int32_t { Rgba8 = 0, Rgba16Float = 1, Depth32 = 2, R11G11B10Float = 3, R32Float = 4 };
enum class Lifetime : int32_t { Imported = 0, Persistent = 1, Transient = 2 };
enum class ImportSource : int32_t { None = 0, SceneColor = 1, LinearDepth = 2 };
enum class Operation : int32_t {
    ClearColor = 0, CopyColor = 1, ClearDepth = 2, ResolveColor = 3,
    VisualizeLinearDepth = 4, BlendColor = 5, AdjustSaturation = 6, ScaleColor = 7,
    TintColor = 8, Fxaa = 9, Sharpen = 10, ChromaticAberration = 11,
    NormalsFromDepth = 12
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
    float saturation = DEFAULT_SATURATION;
    float color_scale = DEFAULT_COLOR_SCALE;
    float tint_red = DEFAULT_TINT_CHANNEL;
    float tint_green = DEFAULT_TINT_CHANNEL;
    float tint_blue = DEFAULT_TINT_CHANNEL;
    float tint_alpha = DEFAULT_TINT_ALPHA;
    float sharpen_amount = DEFAULT_SHARPEN_AMOUNT;
    float chromatic_aberration = DEFAULT_CHROMATIC_ABERRATION;
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
