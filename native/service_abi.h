#pragma once

// Stable, vendor-free metadata for an Elisa-hosted service. The generated
// game header carries function declarations; this descriptor is the explicit
// negotiation and validation layer around those calls.
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    ELISA_SERVICE_ABI_VERSION = 1u,
    ELISA_SERVICE_FEATURE_INPUT = 1ull << 0,
    ELISA_SERVICE_FEATURE_WORLD_QUERY = 1ull << 1,
    ELISA_SERVICE_FEATURE_STATUS = 1ull << 2,
};

typedef struct ElisaAbiSpan {
    const uint8_t* data;
    size_t size;
} ElisaAbiSpan;

typedef struct ElisaServiceDescriptor {
    uint32_t struct_size;
    uint32_t abi_version;
    uint64_t feature_bits;
    uint32_t max_span_bytes;
    uint32_t reserved;
} ElisaServiceDescriptor;

typedef enum ElisaServiceStatus {
    ELISA_SERVICE_OK = 0,
    ELISA_SERVICE_INVALID_ARGUMENT = 1,
    ELISA_SERVICE_UNSUPPORTED_VERSION = 2,
    ELISA_SERVICE_UNSUPPORTED_FEATURE = 3,
} ElisaServiceStatus;

static inline ElisaServiceStatus elisa_validate_span(ElisaAbiSpan span, size_t max_bytes) {
    if (span.size > max_bytes || (span.size != 0 && span.data == NULL)) {
        return ELISA_SERVICE_INVALID_ARGUMENT;
    }
    return ELISA_SERVICE_OK;
}

static inline ElisaServiceStatus elisa_validate_descriptor(
    const ElisaServiceDescriptor* descriptor, uint64_t required_features) {
    if (descriptor == NULL || descriptor->struct_size < sizeof(ElisaServiceDescriptor) ||
        descriptor->reserved != 0 || descriptor->max_span_bytes == 0) {
        return ELISA_SERVICE_INVALID_ARGUMENT;
    }
    if (descriptor->abi_version != ELISA_SERVICE_ABI_VERSION) {
        return ELISA_SERVICE_UNSUPPORTED_VERSION;
    }
    if ((descriptor->feature_bits & required_features) != required_features) {
        return ELISA_SERVICE_UNSUPPORTED_FEATURE;
    }
    return ELISA_SERVICE_OK;
}

#ifdef __cplusplus
}
#endif
