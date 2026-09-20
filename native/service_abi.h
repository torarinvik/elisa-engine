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
    ELISA_SERVICE_FEATURE_SESSION = 1ull << 3,
    ELISA_SERVICE_FEATURE_BOUNDED_QUERY = 1ull << 4,
    ELISA_SERVICE_FEATURE_BACKEND_PROFILE = 1ull << 5,
    ELISA_SERVICE_FEATURE_BACKEND_STATUS = 1ull << 6,
};

typedef struct ElisaAbiSpan {
    const uint8_t* data;
    size_t size;
} ElisaAbiSpan;

typedef enum ElisaServiceStatus {
    ELISA_SERVICE_OK = 0,
    ELISA_SERVICE_INVALID_ARGUMENT = 1,
    ELISA_SERVICE_UNSUPPORTED_VERSION = 2,
    ELISA_SERVICE_UNSUPPORTED_FEATURE = 3,
    ELISA_SERVICE_INVALID_HANDLE = 4,
    ELISA_SERVICE_BUFFER_TOO_SMALL = 5,
} ElisaServiceStatus;

typedef uint64_t ElisaServiceHandle;

typedef struct ElisaAbiBuffer {
    uint8_t* data;
    size_t size;
    size_t capacity;
} ElisaAbiBuffer;

typedef enum ElisaServiceThreadAffinity {
    ELISA_SERVICE_THREAD_CALLER = 0,
    ELISA_SERVICE_THREAD_MAIN = 1,
} ElisaServiceThreadAffinity;

typedef struct ElisaServiceAllocator {
    void* context;
    void* (*allocate)(void* context, size_t bytes, size_t alignment);
    void (*release)(void* context, void* memory, size_t bytes, size_t alignment);
} ElisaServiceAllocator;

typedef struct ElisaServiceV1 {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t callback_thread;
    uint32_t reserved;
    void* context;
    ElisaServiceAllocator allocator;
    ElisaServiceStatus (*create)(void* context, ElisaServiceHandle* out_handle);
    ElisaServiceStatus (*update)(void* context, ElisaServiceHandle handle, ElisaAbiSpan input);
    ElisaServiceStatus (*query)(void* context, ElisaServiceHandle handle, uint32_t query, ElisaAbiBuffer* output);
    ElisaServiceStatus (*destroy)(void* context, ElisaServiceHandle handle);
} ElisaServiceV1;

typedef struct ElisaServiceDescriptor {
    uint32_t struct_size;
    uint32_t abi_version;
    uint64_t feature_bits;
    uint32_t max_span_bytes;
    uint32_t reserved;
} ElisaServiceDescriptor;

static inline ElisaServiceStatus elisa_validate_span(ElisaAbiSpan span, size_t max_bytes) {
    if (span.size > max_bytes || (span.size != 0 && span.data == NULL)) {
        return ELISA_SERVICE_INVALID_ARGUMENT;
    }
    return ELISA_SERVICE_OK;
}

static inline ElisaServiceStatus elisa_validate_buffer(ElisaAbiBuffer buffer, size_t max_bytes) {
    if (buffer.size > buffer.capacity || buffer.capacity > max_bytes ||
        (buffer.capacity != 0 && buffer.data == NULL)) {
        return ELISA_SERVICE_INVALID_ARGUMENT;
    }
    return ELISA_SERVICE_OK;
}

static inline ElisaServiceStatus elisa_validate_allocator(const ElisaServiceAllocator* allocator) {
    if (allocator == NULL || allocator->allocate == NULL || allocator->release == NULL) {
        return ELISA_SERVICE_INVALID_ARGUMENT;
    }
    return ELISA_SERVICE_OK;
}

static inline ElisaServiceStatus elisa_validate_service_v1(const ElisaServiceV1* service) {
    if (service == NULL || service->struct_size < sizeof(ElisaServiceV1) || service->reserved != 0 ||
        service->callback_thread > ELISA_SERVICE_THREAD_MAIN || service->create == NULL ||
        service->update == NULL || service->query == NULL || service->destroy == NULL) {
        return ELISA_SERVICE_INVALID_ARGUMENT;
    }
    if (service->abi_version != ELISA_SERVICE_ABI_VERSION) {
        return ELISA_SERVICE_UNSUPPORTED_VERSION;
    }
    return elisa_validate_allocator(&service->allocator);
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
