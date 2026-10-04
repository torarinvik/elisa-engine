#pragma once

// Text and 2D overlay declarations, included inside the extern "C" block of
// render_scene_abi.h. Include render_scene_abi.h rather than this file.
#include <stdint.h>

int64_t elisa_render_scene_v1_create_text(const char* text, float x, float y,
    int32_t font_size, float red, float green, float blue, float alpha);
int32_t elisa_render_scene_v1_set_text(int64_t handle, const char* text);
int32_t elisa_render_scene_v1_set_text_utf8(int64_t handle, const uint8_t* bytes, uint32_t length);
int32_t elisa_render_scene_v1_measure_text_utf8(const uint8_t* bytes, uint32_t length, int32_t font_size, float* width, float* height, int32_t* ready);
int32_t elisa_render_scene_v1_add_font(const char* asset_path);
int32_t elisa_render_scene_v1_set_text_i64(int64_t handle, const char* prefix, int64_t value);
int32_t elisa_render_scene_v1_set_text_position(int64_t handle, float x, float y);
int32_t elisa_render_scene_v1_set_text_size(int64_t handle, int32_t font_size);
int32_t elisa_render_scene_v1_set_text_alignment(
    int64_t handle, int32_t horizontal, int32_t vertical);
int32_t elisa_render_scene_v1_set_text_color(
    int64_t handle, float red, float green, float blue, float alpha);
int32_t elisa_render_scene_v1_set_text_visible(int64_t handle, int32_t visible);
int32_t elisa_render_scene_v1_destroy_text(int64_t handle);
int32_t elisa_render_scene_v1_set_text_camera(int64_t handle, int64_t camera_handle);
int32_t elisa_render_scene_v1_clear_text_camera(int64_t handle);
int64_t elisa_render_scene_v1_create_overlay_panel(float x, float y,
    float width, float height, float red, float green, float blue, float alpha);
int32_t elisa_render_scene_v1_set_overlay_panel_position(int64_t handle, float x, float y);
int32_t elisa_render_scene_v1_set_overlay_panel_size(int64_t handle, float width, float height);
int32_t elisa_render_scene_v1_set_overlay_panel_color(
    int64_t handle, float red, float green, float blue, float alpha);
int32_t elisa_render_scene_v1_set_overlay_panel_visible(int64_t handle, int32_t visible);
int32_t elisa_render_scene_v1_destroy_overlay_panel(int64_t handle);
int32_t elisa_render_scene_v1_set_overlay_panel_camera(int64_t handle, int64_t camera_handle);
int32_t elisa_render_scene_v1_clear_overlay_panel_camera(int64_t handle);
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
int32_t elisa_render_scene_v1_set_overlay_image_camera(int64_t handle, int64_t camera_handle);
int32_t elisa_render_scene_v1_clear_overlay_image_camera(int64_t handle);
int64_t elisa_render_scene_v1_create_electric_arc(float width, float amplitude, uint32_t seed);
int32_t elisa_render_scene_v1_update_electric_arc(int64_t handle,
    float start_x, float start_y, float start_z,
    float end_x, float end_y, float end_z, float phase, float visibility);
int32_t elisa_render_scene_v1_set_electric_arc_depth_test(int64_t handle, int32_t enabled);
int32_t elisa_render_scene_v1_set_electric_arc_visible(int64_t handle, int32_t visible);
int32_t elisa_render_scene_v1_destroy_electric_arc(int64_t handle);
