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

enum {
    ELISA_PHYSICS_CONTACT_ADDED = 0,
    ELISA_PHYSICS_CONTACT_PERSISTED = 1,
    ELISA_PHYSICS_CONTACT_REMOVED = 2,
    ELISA_PHYSICS_MAX_CONTACT_EVENTS = 64,
};

// This is a deliberately flat copy type. It contains no Wicked or Jolt
// pointers, so the Elisa side can retain events until the next poll call.
typedef struct ElisaPhysicsContactEvent {
    uint64_t entity_a;
    uint64_t entity_b;
    float position_x;
    float position_y;
    float position_z;
    float normal_x;
    float normal_y;
    float normal_z;
    float penetration_depth;
    int32_t kind;
    int32_t trigger;
    uint64_t sequence;
} ElisaPhysicsContactEvent;

int32_t elisa_physics_v1_initialize(uint64_t* world_generation);
int32_t elisa_physics_v1_probe_provider(void);
int32_t elisa_physics_v1_create_box(uint64_t world_generation, int32_t kind,
    float position_x, float position_y, float position_z,
    float half_x, float half_y, float half_z, float mass,
    uint32_t* slot, uint64_t* body_generation);
int32_t elisa_physics_v1_fixed_step(uint64_t world_generation, float delta_seconds,
    uint64_t* tick);
int32_t elisa_physics_v1_poll_contacts(uint64_t world_generation,
    ElisaPhysicsContactEvent* events, uint32_t capacity, uint32_t* count,
    uint32_t* dropped);
int32_t elisa_physics_v1_contact_count(uint64_t world_generation,
    uint32_t* count, uint32_t* dropped);
int32_t elisa_physics_v1_contact_at(uint64_t world_generation, uint32_t index,
    uint64_t* entity_a, uint64_t* entity_b,
    float* position_x, float* position_y, float* position_z,
    float* normal_x, float* normal_y, float* normal_z,
    float* penetration_depth, int32_t* kind, int32_t* trigger, uint64_t* sequence);
int32_t elisa_physics_v1_clear_contacts(uint64_t world_generation);
int32_t elisa_physics_v1_body_position(uint64_t world_generation, uint32_t slot,
    uint64_t body_generation, float* x, float* y, float* z);
int32_t elisa_physics_v1_set_sleeping(uint64_t world_generation, uint32_t slot,
    uint64_t body_generation, int32_t sleeping);
int32_t elisa_physics_v1_set_kinematic_target(uint64_t world_generation, uint32_t slot,
    uint64_t body_generation, float position_x, float position_y, float position_z,
    float rotation_x, float rotation_y, float rotation_z, float rotation_w);
int32_t elisa_physics_v1_destroy_body(uint64_t world_generation, uint32_t slot,
    uint64_t body_generation);
int32_t elisa_physics_v1_shutdown(uint64_t world_generation);

// Called while the Application host is still initialized, before Wicked exits.
void elisa_physics_v1_shutdown_from_application(void);

#ifdef __cplusplus
}
#endif
