#pragma once

// Flat application-owned boundary for the opt-in Wicked/Jolt physics world.
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    ELISA_PHYSICS_OK = 0,
    ELISA_PHYSICS_INVALID_ARGUMENT = -1,
    ELISA_PHYSICS_INVALID_STATE = -2,
    ELISA_PHYSICS_WRONG_THREAD = -3,
    ELISA_PHYSICS_CAPACITY = -4,
    ELISA_PHYSICS_INVALID_HANDLE = -5,
    ELISA_PHYSICS_BACKEND_FAILURE = -6,
    ELISA_PHYSICS_STALE_WORLD = -7,
};

enum {
    ELISA_PHYSICS_BODY_STATIC = 0,
    ELISA_PHYSICS_BODY_KINEMATIC = 1,
    ELISA_PHYSICS_BODY_DYNAMIC = 2,
};

int32_t elisa_physics_v1_initialize(uint64_t* world_generation);
int32_t elisa_physics_v1_probe_provider(void);
int32_t elisa_physics_v1_create_box(uint64_t world_generation, int32_t kind,
    float position_x, float position_y, float position_z,
    float half_x, float half_y, float half_z, float mass,
    uint32_t* slot, uint64_t* body_generation);
int32_t elisa_physics_v1_fixed_step(uint64_t world_generation, float delta_seconds,
    uint64_t* tick);
int32_t elisa_physics_v1_body_position(uint64_t world_generation, uint32_t slot,
    uint64_t body_generation, float* x, float* y, float* z);
int32_t elisa_physics_v1_destroy_body(uint64_t world_generation, uint32_t slot,
    uint64_t body_generation);
int32_t elisa_physics_v1_shutdown(uint64_t world_generation);

// Called while the Application host is still initialized, before Wicked exits.
void elisa_physics_v1_shutdown_from_application(void);

#ifdef __cplusplus
}
#endif
