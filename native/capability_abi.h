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
    ELISA_CAPABILITY_ABI_VERSION = 1u,
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
};

typedef enum ElisaCapabilityStatus {
    ELISA_CAPABILITY_OK = 0,
    ELISA_CAPABILITY_INVALID_ARGUMENT = 1,
    ELISA_CAPABILITY_UNSUPPORTED_VERSION = 2,
} ElisaCapabilityStatus;

typedef struct ElisaBackendProfile {
    uint32_t struct_size;
    uint32_t abi_version;
    uint64_t capability_bits;
    uint64_t optional_bits;
    uint32_t max_viewports;
    uint32_t max_workers;
    uint64_t memory_budget_bytes;
    uint64_t memory_usage_bytes;
} ElisaBackendProfile;

static inline ElisaCapabilityStatus elisa_validate_backend_profile(
    const ElisaBackendProfile* profile) {
    if (profile == NULL || profile->struct_size < sizeof(ElisaBackendProfile) ||
        profile->max_viewports == 0 || profile->max_workers == 0 ||
        profile->memory_usage_bytes > profile->memory_budget_bytes) {
        return ELISA_CAPABILITY_INVALID_ARGUMENT;
    }
    if (profile->abi_version != ELISA_CAPABILITY_ABI_VERSION) {
        return ELISA_CAPABILITY_UNSUPPORTED_VERSION;
    }
    return ELISA_CAPABILITY_OK;
}

#ifdef __cplusplus
}
#endif
