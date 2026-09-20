#pragma once

// Vendor-free capability metadata passed from a native host to Elisa policy.
// Optional features are separate from required service bits so a fallback can
// be selected without pretending that an unavailable device feature exists.
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    ELISA_CAPABILITY_ABI_VERSION = 2u,
    ELISA_CAPABILITY_INPUT = 1ull << 0,
    ELISA_CAPABILITY_RENDERING = 1ull << 1,
    ELISA_CAPABILITY_PHYSICS = 1ull << 2,
    ELISA_CAPABILITY_AUDIO = 1ull << 3,
    ELISA_CAPABILITY_NATIVE_WINDOW = 1ull << 4,
    ELISA_CAPABILITY_ASYNC_UPLOAD = 1ull << 5,
    ELISA_CAPABILITY_CALLBACKS = 1ull << 6,
    ELISA_CAPABILITY_ASSET_LOADING = 1ull << 7,
    ELISA_OPTIONAL_RAYTRACING = 1ull << 0,
    ELISA_OPTIONAL_SPARSE_TEXTURES = 1ull << 1,
    ELISA_OPTIONAL_MESH_SHADERS = 1ull << 2,
    ELISA_FORMAT_RGBA8 = 1ull << 0,
    ELISA_FORMAT_BC1 = 1ull << 1,
    ELISA_FORMAT_R16_FLOAT = 1ull << 2,
};

typedef enum ElisaCapabilityStatus {
    ELISA_CAPABILITY_OK = 0,
    ELISA_CAPABILITY_INVALID_ARGUMENT = 1,
    ELISA_CAPABILITY_UNSUPPORTED_VERSION = 2,
} ElisaCapabilityStatus;

// Stable values returned by maze_backend_missing_feature(). Zero means the
// configured host satisfies the game's required services.
typedef enum ElisaBackendFeatureCode {
    ELISA_BACKEND_FEATURE_NONE = 0,
    ELISA_BACKEND_FEATURE_INPUT = 1,
    ELISA_BACKEND_FEATURE_RENDERING = 2,
    ELISA_BACKEND_FEATURE_PHYSICS = 3,
    ELISA_BACKEND_FEATURE_AUDIO = 4,
    ELISA_BACKEND_FEATURE_NATIVE_WINDOW = 5,
    ELISA_BACKEND_FEATURE_ASYNC_UPLOAD = 6,
    ELISA_BACKEND_FEATURE_CALLBACKS = 7,
    ELISA_BACKEND_FEATURE_ASSET_LOADING = 8
} ElisaBackendFeatureCode;

typedef enum ElisaBackendLimit {
    ELISA_LIMIT_VIEWPORTS = 0,
    ELISA_LIMIT_WORKERS = 1,
    ELISA_LIMIT_GRAPHICS_WORKERS = 2,
    ELISA_LIMIT_STREAMING_WORKERS = 3,
    ELISA_LIMIT_MEMORY_BUDGET_BYTES = 4,
    ELISA_LIMIT_MEMORY_USAGE_BYTES = 5,
    ELISA_LIMIT_MEMORY_AVAILABLE_BYTES = 6,
} ElisaBackendLimit;

typedef struct ElisaBackendProfile {
    uint32_t struct_size;
    uint32_t abi_version;
    uint64_t capability_bits;
    uint64_t optional_bits;
    uint32_t max_viewports;
    uint32_t max_workers;
    uint64_t memory_budget_bytes;
    uint64_t memory_usage_bytes;
    uint64_t resource_format_bits;
    uint32_t graphics_workers;
    uint32_t streaming_workers;
} ElisaBackendProfile;

static inline ElisaCapabilityStatus elisa_validate_backend_profile(
    const ElisaBackendProfile* profile) {
    if (profile == NULL || profile->struct_size < sizeof(ElisaBackendProfile) ||
        profile->max_viewports == 0 || profile->max_workers == 0 ||
        profile->graphics_workers == 0 || profile->streaming_workers == 0 ||
        profile->memory_usage_bytes > profile->memory_budget_bytes) {
        return ELISA_CAPABILITY_INVALID_ARGUMENT;
    }
    if (profile->abi_version != ELISA_CAPABILITY_ABI_VERSION) {
        return ELISA_CAPABILITY_UNSUPPORTED_VERSION;
    }
    const uint64_t known_capabilities = ELISA_CAPABILITY_INPUT | ELISA_CAPABILITY_RENDERING |
        ELISA_CAPABILITY_PHYSICS | ELISA_CAPABILITY_AUDIO | ELISA_CAPABILITY_NATIVE_WINDOW |
        ELISA_CAPABILITY_ASYNC_UPLOAD | ELISA_CAPABILITY_CALLBACKS | ELISA_CAPABILITY_ASSET_LOADING;
    const uint64_t known_optional = ELISA_OPTIONAL_RAYTRACING | ELISA_OPTIONAL_SPARSE_TEXTURES |
        ELISA_OPTIONAL_MESH_SHADERS;
    const uint64_t known_formats = ELISA_FORMAT_RGBA8 | ELISA_FORMAT_BC1 | ELISA_FORMAT_R16_FLOAT;
    if ((profile->capability_bits & ~known_capabilities) != 0 ||
        (profile->optional_bits & ~known_optional) != 0 ||
        (profile->resource_format_bits & ~known_formats) != 0 ||
        (profile->optional_bits != 0 && (profile->capability_bits & ELISA_CAPABILITY_RENDERING) == 0) ||
        (profile->resource_format_bits != 0 && (profile->capability_bits & ELISA_CAPABILITY_RENDERING) == 0)) {
        return ELISA_CAPABILITY_INVALID_ARGUMENT;
    }
    return ELISA_CAPABILITY_OK;
}

static inline int elisa_backend_profile_supports_capability(
    const ElisaBackendProfile* profile, uint64_t capability) {
    if (elisa_validate_backend_profile(profile) != ELISA_CAPABILITY_OK || capability == 0 ||
        (capability & (capability - 1)) != 0) return 0;
    return (profile->capability_bits & capability) != 0;
}

static inline int elisa_backend_profile_supports_format(
    const ElisaBackendProfile* profile, uint64_t format) {
    if (elisa_validate_backend_profile(profile) != ELISA_CAPABILITY_OK || format == 0 ||
        (format & (format - 1)) != 0) return 0;
    return (profile->resource_format_bits & format) != 0;
}

static inline int elisa_backend_profile_limit(const ElisaBackendProfile* profile,
    ElisaBackendLimit limit, uint64_t* value) {
    if (elisa_validate_backend_profile(profile) != ELISA_CAPABILITY_OK || value == NULL) return 0;
    switch (limit) {
        case ELISA_LIMIT_VIEWPORTS: *value = profile->max_viewports; return 1;
        case ELISA_LIMIT_WORKERS: *value = profile->max_workers; return 1;
        case ELISA_LIMIT_GRAPHICS_WORKERS: *value = profile->graphics_workers; return 1;
        case ELISA_LIMIT_STREAMING_WORKERS: *value = profile->streaming_workers; return 1;
        case ELISA_LIMIT_MEMORY_BUDGET_BYTES: *value = profile->memory_budget_bytes; return 1;
        case ELISA_LIMIT_MEMORY_USAGE_BYTES: *value = profile->memory_usage_bytes; return 1;
        case ELISA_LIMIT_MEMORY_AVAILABLE_BYTES:
            *value = profile->memory_budget_bytes - profile->memory_usage_bytes;
            return 1;
        default: return 0;
    }
}

#ifdef __cplusplus
}
#endif
