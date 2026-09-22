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
    // An asset named by a snapshot row or material is still loading.
    ELISA_RENDER_SCENE_ASSET_PENDING = -11,
};

// elisa_render_scene_v1_snapshot_asset_state results; a failed request
// returns its failure status instead.
enum {
    ELISA_RENDER_SCENE_SNAPSHOT_ASSET_ABSENT = 0,
    ELISA_RENDER_SCENE_SNAPSHOT_ASSET_LOADING = 1,
    ELISA_RENDER_SCENE_SNAPSHOT_ASSET_RESIDENT = 2,
};

enum {
    ELISA_RENDER_SCENE_SNAPSHOT_ASSET_MESH = 0,
    ELISA_RENDER_SCENE_SNAPSHOT_ASSET_TEXTURE = 1,
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
int32_t elisa_render_scene_v1_set_camera_render_target(
    int32_t width, int32_t height, float update_interval);
int32_t elisa_render_scene_v1_clear_camera_render_target(void);
int32_t elisa_render_scene_v1_set_camera_orthographic_height(float vertical_size);
int32_t elisa_render_scene_v1_set_camera_perspective(
    float vertical_fov_radians, float near_clip, float far_clip);
int32_t elisa_render_scene_v1_set_camera_look_at(
    float eye_x, float eye_y, float eye_z,
    float target_x, float target_y, float target_z,
    float up_x, float up_y, float up_z);
int32_t elisa_render_scene_v1_camera_ray(
    float screen_x, float screen_y,
    float* origin_x, float* origin_y, float* origin_z,
    float* direction_x, float* direction_y, float* direction_z);
int64_t elisa_render_scene_v1_create_camera(
    int32_t projection, int32_t width, int32_t height,
    float projection_value, float near_clip, float far_clip);
int32_t elisa_render_scene_v1_update_camera(
    int64_t handle, int32_t projection, int32_t width, int32_t height,
    float projection_value, float near_clip, float far_clip);
int32_t elisa_render_scene_v1_activate_camera(int64_t handle);
int32_t elisa_render_scene_v1_activate_default_camera(void);
int32_t elisa_render_scene_v1_destroy_camera(int64_t handle);
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
// Draws source's mesh and material again at another transform. Wicked renders
// every object naming one mesh entity from that mesh's buffers, so repeated
// props load and upload once. The color multiplies the source material's base
// color per instance. A clone has no material of its own (material calls on
// it return BACKEND_FAILED), and a skinned, animated, morphing or cloned
// source returns INVALID_ARGUMENT. Destroying the source first keeps its mesh
// until the last clone is destroyed.
int64_t elisa_render_scene_v1_create_mesh_instance(
    int64_t source_handle,
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
// A row with a nonzero existing_handle retains that instance and must name its
// gameplay epoch/ID, render ID, mesh ID, and material or material-set ID, or
// staging returns INVALID_ARGUMENT. Commit updates only its transform and tint;
// to change the mesh, material, or identity, retire it and stage a new row.
int32_t elisa_render_scene_v1_snapshot_stage(
    int64_t existing_handle, int64_t gameplay_epoch, int64_t gameplay_id, int64_t render_id,
    uint64_t mesh_high, uint64_t mesh_low, uint64_t material_high, uint64_t material_low,
    float px, float py, float pz, float qx, float qy, float qz, float qw,
    float sx, float sy, float sz);
// Override the ObjectComponent color and emissive multipliers for a row staged
// earlier in this transaction. Rows without a call commit neutral.
int32_t elisa_render_scene_v1_snapshot_stage_tint(int64_t render_id,
    float red, float green, float blue,
    float emissive_red, float emissive_green, float emissive_blue, float emissive_strength);
int32_t elisa_render_scene_v1_snapshot_retire(int64_t handle);
int32_t elisa_render_scene_v1_snapshot_commit(void);
int32_t elisa_render_scene_v1_snapshot_abort(void);
int64_t elisa_render_scene_v1_snapshot_result(uint32_t index);
int32_t elisa_render_scene_v1_register_snapshot_mesh_asset(
    uint64_t high, uint64_t low, const char* package_path);
int32_t elisa_render_scene_v1_unregister_snapshot_mesh_asset(uint64_t high, uint64_t low);
int32_t elisa_render_scene_v1_register_snapshot_texture_asset(
    uint64_t high, uint64_t low, const char* asset_path);
int32_t elisa_render_scene_v1_unregister_snapshot_texture_asset(uint64_t high, uint64_t low);
// Register a PNG or JPEG section of a cooked ELPK bundle as a snapshot texture.
int32_t elisa_render_scene_v1_register_snapshot_bundle_texture_asset(
    uint64_t high, uint64_t low, const char* bundle_path, const char* section);
// Asynchronous forms: the request returns without file IO, a worker thread
// reads and decodes bundle images, and pump adopts up to `budget` finished
// loads on the owner thread, returning how many became resident.
int32_t elisa_render_scene_v1_request_snapshot_mesh_asset(
    uint64_t high, uint64_t low, const char* package_path);
int32_t elisa_render_scene_v1_request_snapshot_mesh_asset_with_priority(
    uint64_t high, uint64_t low, const char* package_path, int32_t priority);
int32_t elisa_render_scene_v1_request_snapshot_bundle_texture_asset(
    uint64_t high, uint64_t low, const char* bundle_path, const char* section);
int32_t elisa_render_scene_v1_request_snapshot_bundle_texture_asset_with_priority(
    uint64_t high, uint64_t low, const char* bundle_path, const char* section, int32_t priority);
int32_t elisa_render_scene_v1_pump_snapshot_assets(uint32_t budget);
int32_t elisa_render_scene_v1_snapshot_asset_state(int32_t kind, uint64_t high, uint64_t low);
int32_t elisa_render_scene_v1_register_snapshot_material_asset(
    uint64_t high, uint64_t low,
    float red, float green, float blue, float alpha,
    float metallic, float roughness,
    float emissive_red, float emissive_green, float emissive_blue,
    float alpha_cutoff, int32_t alpha_mode, int32_t double_sided);
int32_t elisa_render_scene_v1_register_snapshot_material_asset_with_textures(
    uint64_t high, uint64_t low,
    uint64_t base_color_high, uint64_t base_color_low,
    uint64_t normal_high, uint64_t normal_low,
    uint64_t surface_high, uint64_t surface_low,
    uint64_t emissive_high, uint64_t emissive_low,
    float red, float green, float blue, float alpha,
    float metallic, float roughness,
    float emissive_red, float emissive_green, float emissive_blue,
    float alpha_cutoff, int32_t alpha_mode, int32_t double_sided);
// The textured form with an occlusion flag, 0 or 1: occlusion reads the
// surface texture's red channel, so it needs a surface texture. Occlusion is
// part of the registration values, and the forms above register without it.
int32_t elisa_render_scene_v1_register_snapshot_material_asset_with_occlusion(
    uint64_t high, uint64_t low,
    uint64_t base_color_high, uint64_t base_color_low,
    uint64_t normal_high, uint64_t normal_low,
    uint64_t surface_high, uint64_t surface_low,
    uint64_t emissive_high, uint64_t emissive_low,
    float red, float green, float blue, float alpha,
    float metallic, float roughness,
    float emissive_red, float emissive_green, float emissive_blue,
    float alpha_cutoff, int32_t alpha_mode, int32_t double_sided, int32_t occlusion);
int32_t elisa_render_scene_v1_unregister_snapshot_material_asset(uint64_t high, uint64_t low);
int32_t elisa_render_scene_v1_stage_snapshot_material_set_slot(
    uint32_t slot, uint64_t material_high, uint64_t material_low);
int32_t elisa_render_scene_v1_register_snapshot_material_set(uint64_t high, uint64_t low, uint32_t count);
int32_t elisa_render_scene_v1_unregister_snapshot_material_set(uint64_t high, uint64_t low);
// Cooked slot materials: a resident mesh's count is 0 when its source authored
// none. Registration follows the material rules; the set call consumes the
// staged slots and, on failure, removes every material it created.
int32_t elisa_render_scene_v1_snapshot_mesh_material_count(uint64_t mesh_high, uint64_t mesh_low);
int32_t elisa_render_scene_v1_snapshot_mesh_placement_count(uint64_t mesh_high, uint64_t mesh_low);
int32_t elisa_render_scene_v1_snapshot_mesh_placement(
    uint64_t mesh_high, uint64_t mesh_low, uint32_t index,
    uint32_t* mesh_index, uint32_t* node_index);
float elisa_render_scene_v1_snapshot_mesh_placement_component(
    uint64_t mesh_high, uint64_t mesh_low, uint32_t index, uint32_t component);
int32_t elisa_render_scene_v1_register_snapshot_mesh_material(
    uint64_t mesh_high, uint64_t mesh_low, uint32_t slot, uint64_t high, uint64_t low);
int32_t elisa_render_scene_v1_register_snapshot_mesh_material_set(
    uint64_t mesh_high, uint64_t mesh_low, uint64_t set_high, uint64_t set_low, uint32_t count);
// Cooked slot textures: the images a mesh's slot materials sample, each
// registered as a bundle texture from the mesh's own bundle before the
// materials that sample it.
int32_t elisa_render_scene_v1_snapshot_mesh_texture_count(uint64_t mesh_high, uint64_t mesh_low);
int32_t elisa_render_scene_v1_register_snapshot_mesh_texture(
    uint64_t mesh_high, uint64_t mesh_low, uint32_t index, uint64_t high, uint64_t low);
int32_t elisa_render_scene_v1_play_animation(
    int64_t handle, const char* clip_name, int32_t loop, float speed, float blend_seconds);
int32_t elisa_render_scene_v1_play_animation_blended(
    int64_t handle, const char* clip_name, int32_t loop, float speed, float blend_seconds, int32_t flags);
int32_t elisa_render_scene_v1_set_animation_speed(int64_t handle, float speed);
int32_t elisa_render_scene_v1_stop_animation(int64_t handle, float blend_seconds);
int32_t elisa_render_scene_v1_advance_animation(int64_t handle, float delta_seconds);
float elisa_render_scene_v1_animation_progress(int64_t handle);
enum {
    ELISA_RENDER_SCENE_MAX_ANIMATION_BONES = 64u,
    ELISA_RENDER_SCENE_MAX_ANIMATION_MORPHS = 32u,
    ELISA_RENDER_SCENE_ANIMATION_MATRIX_ELEMENTS = 16u,
};
typedef struct ElisaRenderSceneAnimationSubmission {
    float bones[ELISA_RENDER_SCENE_MAX_ANIMATION_BONES * ELISA_RENDER_SCENE_ANIMATION_MATRIX_ELEMENTS];
    float morphs[ELISA_RENDER_SCENE_MAX_ANIMATION_MORPHS];
    uint32_t bone_count;
    uint32_t morph_count;
} ElisaRenderSceneAnimationSubmission;
int32_t elisa_render_scene_v1_submit_animation_pose(
    int64_t handle, const ElisaRenderSceneAnimationSubmission* submission);
int32_t elisa_render_scene_v1_complete_animation_pose(int64_t handle);
int64_t elisa_render_scene_v1_create_effect_emitter(
    float px, float py, float pz, uint32_t max_particles,
    float count, float lifetime, float size,
    float velocity_x, float velocity_y, float velocity_z);
int32_t elisa_render_scene_v1_tick_effect_emitter(int64_t handle, float delta_seconds);
int64_t elisa_render_scene_v1_create_effect_decal(
    float px, float py, float pz,
    float red, float green, float blue, float alpha,
    float range, float slope_blend);
int32_t elisa_render_scene_v1_update_effect_decal(
    int64_t handle, float red, float green, float blue, float alpha,
    float range, float slope_blend);
int32_t elisa_render_scene_v1_destroy_effect(int64_t handle);
int32_t elisa_render_scene_v1_set_lit_material(int64_t handle, float roughness, float metallic);
int32_t elisa_render_scene_v1_set_texture_uv_transform(int64_t handle, float scale_u, float scale_v, float offset_u, float offset_v);
int32_t elisa_render_scene_v1_set_sun_shadows(int32_t enabled);
int32_t elisa_render_scene_v1_set_color(int64_t handle, float red, float green, float blue, float alpha);
int32_t elisa_render_scene_v1_set_alpha_mode(int64_t handle, int32_t mode, float cutoff, int32_t double_sided);
int32_t elisa_render_scene_v1_set_cast_shadow(int64_t handle, int32_t enabled);
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
int64_t elisa_render_scene_v1_create_light(
    int32_t kind,
    float red, float green, float blue,
    float px, float py, float pz,
    float dx, float dy, float dz,
    float intensity, float range, float inner_cone, float outer_cone,
    int32_t casts_shadow);
int32_t elisa_render_scene_v1_update_light(
    int64_t handle, int32_t kind,
    float red, float green, float blue,
    float px, float py, float pz,
    float dx, float dy, float dz,
    float intensity, float range, float inner_cone, float outer_cone,
    int32_t casts_shadow);
int32_t elisa_render_scene_v1_destroy_light(int64_t handle);
int32_t elisa_render_scene_v1_set_bloom(int32_t enabled, float threshold);
int32_t elisa_render_scene_v1_set_ambient_occlusion(int32_t enabled);
int32_t elisa_render_scene_v1_set_ambient_occlusion_settings(float range, float power);
int32_t elisa_render_scene_v1_set_fxaa(int32_t enabled);
int32_t elisa_render_scene_v1_set_tonemap(int32_t tonemap);
int32_t elisa_render_scene_v1_set_exposure(float exposure);
int32_t elisa_render_scene_v1_set_shadow_quality(int32_t quality);
enum {
    ELISA_RENDER_SCENE_QUALITY_TONEMAP_REINHARD = 0,
    ELISA_RENDER_SCENE_QUALITY_TONEMAP_ACES = 1,
    ELISA_RENDER_SCENE_QUALITY_TONEMAP_UCHIMURA = 2,
    ELISA_RENDER_SCENE_QUALITY_UPSCALER_NONE = 0,
    ELISA_RENDER_SCENE_QUALITY_UPSCALER_FSR1 = 1,
    ELISA_RENDER_SCENE_QUALITY_UPSCALER_FSR2 = 2,
};
typedef struct ElisaRenderSceneQualityProfile {
    int32_t tonemap;
    int32_t upscaler;
    float render_scale;
    float bloom_threshold;
    int32_t bloom;
    int32_t fxaa;
    int32_t ambient_occlusion;
    int32_t screen_space_reflections;
    int32_t fog;
    int32_t depth_effects;
} ElisaRenderSceneQualityProfile;
int32_t elisa_render_scene_v1_apply_quality_profile(
    const ElisaRenderSceneQualityProfile* profile);
int32_t elisa_render_scene_v1_set_visible(int64_t handle, int32_t visible);
int32_t elisa_render_scene_v1_set_visibility_policy(
    int64_t handle, float draw_distance, float lod_bias, uint32_t layer_mask, int32_t renderable);
int32_t elisa_render_scene_v1_set_occlusion_culling(int32_t enabled);
int64_t elisa_render_scene_v1_create_text(const char* text, float x, float y,
    int32_t font_size, float red, float green, float blue, float alpha);
int32_t elisa_render_scene_v1_set_text(int64_t handle, const char* text);
int32_t elisa_render_scene_v1_set_text_i64(int64_t handle, const char* prefix, int64_t value);
int32_t elisa_render_scene_v1_set_text_position(int64_t handle, float x, float y);
int32_t elisa_render_scene_v1_set_text_size(int64_t handle, int32_t font_size);
int32_t elisa_render_scene_v1_set_text_alignment(
    int64_t handle, int32_t horizontal, int32_t vertical);
int32_t elisa_render_scene_v1_set_text_color(
    int64_t handle, float red, float green, float blue, float alpha);
int32_t elisa_render_scene_v1_set_text_visible(int64_t handle, int32_t visible);
int32_t elisa_render_scene_v1_destroy_text(int64_t handle);
int64_t elisa_render_scene_v1_create_overlay_panel(float x, float y,
    float width, float height, float red, float green, float blue, float alpha);
int32_t elisa_render_scene_v1_set_overlay_panel_position(int64_t handle, float x, float y);
int32_t elisa_render_scene_v1_set_overlay_panel_size(int64_t handle, float width, float height);
int32_t elisa_render_scene_v1_set_overlay_panel_color(
    int64_t handle, float red, float green, float blue, float alpha);
int32_t elisa_render_scene_v1_set_overlay_panel_visible(int64_t handle, int32_t visible);
int32_t elisa_render_scene_v1_destroy_overlay_panel(int64_t handle);
int64_t elisa_render_scene_v1_create_overlay_image(const char* asset_path,
    float x, float y, float width, float height);
int32_t elisa_render_scene_v1_set_overlay_image_position(int64_t handle, float x, float y);
int32_t elisa_render_scene_v1_set_overlay_image_size(int64_t handle, float width, float height);
int32_t elisa_render_scene_v1_set_overlay_image_uv(int64_t handle,
    float u0, float v0, float u1, float v1);
int32_t elisa_render_scene_v1_set_overlay_image_color(
    int64_t handle, float red, float green, float blue, float alpha);
int32_t elisa_render_scene_v1_set_overlay_image_visible(int64_t handle, int32_t visible);
int32_t elisa_render_scene_v1_destroy_overlay_image(int64_t handle);
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
