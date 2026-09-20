#pragma once

// Engine-owned scalar ABI behind src/runtime/render_scene.elisa. The public
// Elisa API exposes only checked handles and value types, never Wicked objects.
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    ELISA_RENDER_SCENE_ABI_VERSION = 1u,
    ELISA_RENDER_SCENE_OK = 0,
    ELISA_RENDER_SCENE_INVALID_ARGUMENT = -1,
    ELISA_RENDER_SCENE_NOT_INITIALIZED = -2,
    ELISA_RENDER_SCENE_ALREADY_INITIALIZED = -3,
    ELISA_RENDER_SCENE_WRONG_THREAD = -4,
    ELISA_RENDER_SCENE_CAPACITY = -5,
    ELISA_RENDER_SCENE_UNKNOWN_HANDLE = -6,
    ELISA_RENDER_SCENE_GENERATION_EXHAUSTED = -7,
    ELISA_RENDER_SCENE_BACKEND_FAILED = -8,
};

enum {
    ELISA_RENDER_PRIMITIVE_BOX = 1,
    ELISA_RENDER_PRIMITIVE_SPHERE = 2,
    ELISA_RENDER_PRIMITIVE_PLANE = 3,
};

uint32_t elisa_render_scene_abi_version(void);
int32_t elisa_render_scene_v1_initialize(int32_t width, int32_t height, float vertical_size);
int32_t elisa_render_scene_v1_resize(int32_t width, int32_t height);
int32_t elisa_render_scene_v1_set_camera_look_at(
    float eye_x, float eye_y, float eye_z,
    float target_x, float target_y, float target_z,
    float up_x, float up_y, float up_z);
int64_t elisa_render_scene_v1_create(
    int32_t primitive,
    float px, float py, float pz,
    float qx, float qy, float qz, float qw,
    float sx, float sy, float sz,
    float red, float green, float blue, float alpha);
int32_t elisa_render_scene_v1_update_transform(
    int64_t handle,
    float px, float py, float pz,
    float qx, float qy, float qz, float qw,
    float sx, float sy, float sz);
int32_t elisa_render_scene_v1_set_color(int64_t handle, float red, float green, float blue, float alpha);
int32_t elisa_render_scene_v1_set_visible(int64_t handle, int32_t visible);
int32_t elisa_render_scene_v1_destroy(int64_t handle);
int32_t elisa_render_scene_v1_shutdown(void);
uint64_t elisa_render_scene_v1_instance_count(void);
int32_t elisa_render_scene_v1_is_initialized(void);

// Native smoke evidence: checks that rendered scene geometry changes a known
// center pixel from the clear corner pixel.
int32_t elisa_render_scene_v1_last_frame_center_differs_from_corner(void);

#ifdef __cplusplus
}
#endif
