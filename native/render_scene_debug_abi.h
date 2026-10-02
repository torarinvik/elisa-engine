#pragma once

// Debug-draw declarations, included inside the extern "C" block of
// render_scene_abi.h. Include render_scene_abi.h rather than this file.
#include <stdint.h>

int32_t elisa_render_scene_v1_debug_box(
    float min_x, float min_y, float min_z,
    float max_x, float max_y, float max_z,
    float red, float green, float blue, float alpha,
    int32_t depth_tested);
int32_t elisa_render_scene_v1_debug_line(
    float start_x, float start_y, float start_z,
    float end_x, float end_y, float end_z,
    float red, float green, float blue, float alpha,
    int32_t depth_tested);
int32_t elisa_render_scene_v1_debug_text(
    const char* text, float x, float y, float z,
    float red, float green, float blue, float alpha,
    int32_t depth_tested);
enum {
    ELISA_RENDER_SCENE_DEBUG_LINE_CHUNK = 256u,
    ELISA_RENDER_SCENE_DEBUG_LINE_VALUES = 10u,
    ELISA_RENDER_SCENE_DEBUG_MAX_BATCH_LINES = 16384u,
};
/* Start xyz, end xyz and rgba per line; returns the lines queued. */
typedef struct ElisaRenderSceneDebugLineChunk {
    float values[ELISA_RENDER_SCENE_DEBUG_LINE_CHUNK * ELISA_RENDER_SCENE_DEBUG_LINE_VALUES];
    uint32_t count;
    int32_t depth_tested;
} ElisaRenderSceneDebugLineChunk;
int32_t elisa_render_scene_v1_debug_line_chunk(const ElisaRenderSceneDebugLineChunk* chunk);
enum {
    ELISA_RENDER_SCENE_DEBUG_TEXT_CHUNK = 64u,
    ELISA_RENDER_SCENE_DEBUG_TEXT_VALUES = 7u,
    ELISA_RENDER_SCENE_DEBUG_TEXT_STRIDE = 64u,
    ELISA_RENDER_SCENE_DEBUG_MAX_BATCH_TEXTS = 1024u,
};
/* Position xyz and rgba per label, then `lengths[i]` UTF-8 bytes (1..63, no
   zero byte) at `bytes + i * 64`; returns the labels queued. */
typedef struct ElisaRenderSceneDebugTextChunk {
    float values[ELISA_RENDER_SCENE_DEBUG_TEXT_CHUNK * ELISA_RENDER_SCENE_DEBUG_TEXT_VALUES];
    uint8_t bytes[ELISA_RENDER_SCENE_DEBUG_TEXT_CHUNK * ELISA_RENDER_SCENE_DEBUG_TEXT_STRIDE];
    uint32_t lengths[ELISA_RENDER_SCENE_DEBUG_TEXT_CHUNK];
    uint32_t count;
    int32_t depth_tested;
} ElisaRenderSceneDebugTextChunk;
int32_t elisa_render_scene_v1_debug_text_chunk(const ElisaRenderSceneDebugTextChunk* chunk);
int32_t elisa_render_scene_v1_debug_flush(void);
int32_t elisa_render_scene_v1_debug_clear(void);
