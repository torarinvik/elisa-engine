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
    ELISA_RENDER_SCENE_ASSET_LOAD_FAILED = -9,
    ELISA_RENDER_SCENE_BATCH_ACTIVE = -10,
};

enum {
    ELISA_RENDER_PRIMITIVE_BOX = 1,
    ELISA_RENDER_PRIMITIVE_SPHERE = 2,
    ELISA_RENDER_PRIMITIVE_PLANE = 3,
};

enum {
    ELISA_RENDER_SCENE_ALPHA_OPAQUE = 0,
    ELISA_RENDER_SCENE_ALPHA_MASK = 1,
    ELISA_RENDER_SCENE_ALPHA_BLEND = 2,
};

uint32_t elisa_render_scene_abi_version(void);
int32_t elisa_render_scene_v1_initialize(int32_t width, int32_t height, float vertical_size);
int32_t elisa_render_scene_v1_resize(int32_t width, int32_t height);
int32_t elisa_render_scene_v1_set_camera_orthographic_height(float vertical_size);
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
int64_t elisa_render_scene_v1_create_mesh(
    const char* package_path,
    float px, float py, float pz,
    float qx, float qy, float qz, float qw,
    float sx, float sy, float sz,
    float red, float green, float blue, float alpha);
int32_t elisa_render_scene_v1_update_transform(
    int64_t handle,
    float px, float py, float pz,
    float qx, float qy, float qz, float qw,
    float sx, float sy, float sz);
int32_t elisa_render_scene_v1_snapshot_begin(uint32_t previous_count);
int32_t elisa_render_scene_v1_snapshot_stage(
    int64_t existing_handle, int64_t gameplay_epoch, int64_t gameplay_id, int64_t render_id,
    uint64_t mesh_high, uint64_t mesh_low, uint64_t material_high, uint64_t material_low,
    float px, float py, float pz, float qx, float qy, float qz, float qw,
    float sx, float sy, float sz);
int32_t elisa_render_scene_v1_snapshot_retire(int64_t handle);
int32_t elisa_render_scene_v1_snapshot_commit(void);
int32_t elisa_render_scene_v1_snapshot_abort(void);
int64_t elisa_render_scene_v1_snapshot_result(uint32_t index);
int32_t elisa_render_scene_v1_register_snapshot_mesh_asset(
    uint64_t high, uint64_t low, const char* package_path);
int32_t elisa_render_scene_v1_unregister_snapshot_mesh_asset(uint64_t high, uint64_t low);
int32_t elisa_render_scene_v1_register_snapshot_material_asset(
    uint64_t high, uint64_t low,
    float red, float green, float blue, float alpha,
    float metallic, float roughness,
    float emissive_red, float emissive_green, float emissive_blue,
    float alpha_cutoff, int32_t alpha_mode, int32_t double_sided);
int32_t elisa_render_scene_v1_unregister_snapshot_material_asset(uint64_t high, uint64_t low);
int32_t elisa_render_scene_v1_play_animation(
    int64_t handle, const char* clip_name, int32_t loop, float speed, float blend_seconds);
int32_t elisa_render_scene_v1_stop_animation(int64_t handle, float blend_seconds);
int32_t elisa_render_scene_v1_advance_animation(int64_t handle, float delta_seconds);
int32_t elisa_render_scene_v1_set_lit_material(int64_t handle, float roughness, float metallic);
int32_t elisa_render_scene_v1_set_texture_uv_transform(int64_t handle, float scale_u, float scale_v, float offset_u, float offset_v);
int32_t elisa_render_scene_v1_set_sun_shadows(int32_t enabled);
int32_t elisa_render_scene_v1_set_color(int64_t handle, float red, float green, float blue, float alpha);
int32_t elisa_render_scene_v1_set_alpha_mode(int64_t handle, int32_t mode, float cutoff, int32_t double_sided);
// Texture slots: 0 base color, 1 normal, 2 packed surface, 3 emissive.
// Surface RGBA channels are occlusion, roughness, metalness, reflectance.
// Paths are project-relative and resolved inside ELISA_PROJECT_ROOT.
int32_t elisa_render_scene_v1_set_texture(int64_t handle, int32_t slot, const char* asset_path);
int32_t elisa_render_scene_v1_set_emissive(
    int64_t handle, float red, float green, float blue, float strength);
int32_t elisa_render_scene_v1_set_environment(
    float sun_direction_x, float sun_direction_y, float sun_direction_z,
    float sun_red, float sun_green, float sun_blue,
    float ambient_red, float ambient_green, float ambient_blue,
    float sky_exposure,
    float fog_red, float fog_green, float fog_blue,
    float fog_start, float fog_density, int32_t fog_enabled);
int32_t elisa_render_scene_v1_set_bloom(int32_t enabled, float threshold);
int32_t elisa_render_scene_v1_set_visible(int64_t handle, int32_t visible);
int64_t elisa_render_scene_v1_create_text(const char* text, float x, float y,
    int32_t font_size, float red, float green, float blue, float alpha);
int32_t elisa_render_scene_v1_set_text(int64_t handle, const char* text);
int32_t elisa_render_scene_v1_set_text_i64(int64_t handle, const char* prefix, int64_t value);
int32_t elisa_render_scene_v1_set_text_position(int64_t handle, float x, float y);
int32_t elisa_render_scene_v1_set_text_visible(int64_t handle, int32_t visible);
int32_t elisa_render_scene_v1_destroy_text(int64_t handle);
int64_t elisa_render_scene_v1_create_electric_arc(float width, float amplitude, uint32_t seed);
int32_t elisa_render_scene_v1_update_electric_arc(int64_t handle,
    float start_x, float start_y, float start_z,
    float end_x, float end_y, float end_z, float phase, float visibility);
int32_t elisa_render_scene_v1_set_electric_arc_depth_test(int64_t handle, int32_t enabled);
int32_t elisa_render_scene_v1_set_electric_arc_visible(int64_t handle, int32_t visible);
int32_t elisa_render_scene_v1_destroy_electric_arc(int64_t handle);
int32_t elisa_render_scene_v1_destroy(int64_t handle);
int32_t elisa_render_scene_v1_shutdown(void);
uint64_t elisa_render_scene_v1_instance_count(void);
int32_t elisa_render_scene_v1_is_initialized(void);

#ifdef __cplusplus
}
#endif
