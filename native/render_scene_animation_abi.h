#pragma once

// Animation pose readback, override, skin-contact and submission declarations,
// included inside the extern "C" block of render_scene_abi.h. Include
// render_scene_abi.h rather than this file.
#include <stdint.h>

enum {
    ELISA_RENDER_SCENE_MAX_ANIMATION_BONES = 256u,
    ELISA_RENDER_SCENE_MAX_ANIMATION_MORPHS = 32u,
    ELISA_RENDER_SCENE_ANIMATION_MATRIX_ELEMENTS = 16u,
};
// Displayed locals are in Elisa asset space after horizontal root extraction.
// Reading does not advance animation or consume pending root motion.
typedef struct ElisaRenderSceneAnimationReadback {
    float locals[ELISA_RENDER_SCENE_MAX_ANIMATION_BONES * 10u];
    int64_t parents[ELISA_RENDER_SCENE_MAX_ANIMATION_BONES];
    uint32_t count;
} ElisaRenderSceneAnimationReadback;
int32_t elisa_render_scene_v1_read_animation_pose(
    int64_t handle, ElisaRenderSceneAnimationReadback* output);
int32_t elisa_render_scene_v1_animation_joint_index(
    int64_t handle, const char* joint_name, uint32_t* index);
int32_t elisa_render_scene_v1_override_animation_pose(
    int64_t handle, const ElisaRenderSceneAnimationReadback* input);
// Up to 64 requested vertices; each record is model xyz then world xyz.
typedef struct ElisaRenderSceneSkinReadback {
    float points[64u * 6u];
    uint32_t count;
} ElisaRenderSceneSkinReadback;
int32_t elisa_render_scene_v1_read_skinned_vertices(
    int64_t handle, uint32_t placement, const uint32_t* vertices, uint32_t count,
    ElisaRenderSceneSkinReadback* output);
typedef struct ElisaRenderSceneSkinContact {
    float model_point[3];
    float world_point[3];
    float world_center[3];
    float clearance;
    uint32_t vertices;
} ElisaRenderSceneSkinContact;
int32_t elisa_render_scene_v1_set_skin_contact_region(int64_t handle,
    uint32_t region, uint32_t placement, const uint32_t* vertices, uint32_t count);
int32_t elisa_render_scene_v1_set_skin_contact_joints(int64_t handle,
    uint32_t region, uint32_t placement, const uint32_t* joints, uint32_t count, float threshold);
int32_t elisa_render_scene_v1_read_skin_contact(int64_t handle, uint32_t region,
    float nx, float ny, float nz, float offset, ElisaRenderSceneSkinContact* output);
typedef struct ElisaRenderSceneAnimationSubmission {
    float bones[ELISA_RENDER_SCENE_MAX_ANIMATION_BONES * ELISA_RENDER_SCENE_ANIMATION_MATRIX_ELEMENTS];
    float morphs[ELISA_RENDER_SCENE_MAX_ANIMATION_MORPHS];
    uint32_t bone_count;
    uint32_t morph_count;
} ElisaRenderSceneAnimationSubmission;
int32_t elisa_render_scene_v1_submit_animation_pose(
    int64_t handle, const ElisaRenderSceneAnimationSubmission* submission);
#if defined(ELISA_RENDER_SCENE_TEST_PROBE)
int32_t elisa_render_scene_v1_test_animation_root_pose(
    int64_t handle, float expected_x, float expected_y, float expected_z);
#endif
int32_t elisa_render_scene_v1_complete_animation_pose(int64_t handle);
