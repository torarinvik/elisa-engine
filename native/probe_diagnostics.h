#pragma once
// Ground-truth diagnostics for the scene probe: eye/at, visibility counts,
// mesh and object state, and render-target inventory. Pixels alone cannot
// distinguish "camera faces away" from "light missing" from "culled", so the
// probe prints what the renderer actually decided. Kept out of
// native/wicked_probe.cpp so that file stays under the 600-line limit.
#include "wiRenderPath3D.h"
#include "wiRenderer.h"
#include "wiScene.h"

#include <cstdio>

namespace probe {

inline void print_scene_diagnostics(wi::scene::Scene& scene, const wi::RenderPath3D& render_path,
    const wi::scene::MeshComponent* mesh, const wi::scene::CameraComponent* camera_component,
    wi::ecs::Entity object) {
    const XMFLOAT3 eye = camera_component->Eye;
    const XMFLOAT3 at = camera_component->At;
    std::fprintf(stdout, "camera eye=(%.2f,%.2f,%.2f) at=(%.2f,%.2f,%.2f) wh=(%.1f,%.1f) near=%.3f far=%.1f fov=%.3f\n",
        eye.x, eye.y, eye.z, at.x, at.y, at.z,
        camera_component->width, camera_component->height,
        camera_component->zNearP, camera_component->zFarP, camera_component->fov);
    std::fprintf(stdout, "scene objects=%u lights=%u visible objects=%u visible lights=%u\n",
        (unsigned)scene.objects.GetCount(), (unsigned)scene.lights.GetCount(),
        (unsigned)render_path.visibility_main.visibleObjects.size(),
        (unsigned)render_path.visibility_main.visibleLights.size());
    std::fprintf(stdout, "aabb streams=%u\n", (unsigned)scene.aabb_objects.size());
    {
        XMUINT2 internal = render_path.GetInternalResolution();
        std::fprintf(stdout, "internal resolution=%ux%u\n", internal.x, internal.y);
    }
    std::fprintf(stdout, "mesh verts=%u idx-valid=%d pos-valid=%d indices=%u subset-count=%u general-valid=%d clu-valid=%d meshshader-allowed=%d\n",
        (unsigned)mesh->vertex_positions.size(),
        mesh->ib.IsValid() ? 1 : 0, mesh->vb_pos_wind.IsValid() ? 1 : 0,
        (unsigned)mesh->indices.size(),
        mesh->subsets.empty() ? 0u : (unsigned)mesh->subsets[0].indexCount,
        mesh->generalBuffer.IsValid() ? 1 : 0,
        mesh->vb_clu.IsValid() ? 1 : 0,
        wi::renderer::IsMeshShaderAllowed() ? 1 : 0);
    {
        const auto* drawable = scene.objects.GetComponent(object);
        std::fprintf(stdout, "object renderable=%d foreground=%d hide-main=%d filtermask=%u mesh-index=%u\n",
            (drawable != nullptr && drawable->IsRenderable()) ? 1 : 0,
            (drawable != nullptr && drawable->IsForeground()) ? 1 : 0,
            (drawable != nullptr && drawable->IsNotVisibleInMainCamera()) ? 1 : 0,
            drawable != nullptr ? drawable->GetFilterMask() : 0u,
            drawable != nullptr ? drawable->mesh_index : 9999u);
        if (!render_path.visibility_main.visibleObjects.empty()) {
            std::fprintf(stdout, "visible[0]=%u\n",
                (unsigned)render_path.visibility_main.visibleObjects[0]);
        }
    }
    if (!scene.aabb_objects.empty()) {
        const auto& bounds = scene.aabb_objects[0];
        std::fprintf(stdout, "aabb0 min=(%.2f,%.2f,%.2f) max=(%.2f,%.2f,%.2f) layer=%u\n",
            bounds._min.x, bounds._min.y, bounds._min.z,
            bounds._max.x, bounds._max.y, bounds._max.z, bounds.layerMask);
    }
    std::fprintf(stdout, "instance matrices=%u\n", (unsigned)scene.matrix_objects.size());
    if (!scene.matrix_objects.empty()) {
        const auto& instance = scene.matrix_objects[0];
        std::fprintf(stdout, "instance0 row=(%.2f,%.2f,%.2f,%.2f)\n",
            instance.m[3][0], instance.m[3][1], instance.m[3][2], instance.m[3][3]);
    }
    // Render-target inventory: dimensions and sample counts pin down sizing
    // questions without downloading anything the graphics API forbids
    // reading back.
    std::fprintf(stdout, "rtMain %ux%u samples=%u msaa %ux%u samples=%u\n",
        render_path.rtMain.desc.width, render_path.rtMain.desc.height,
        render_path.rtMain.desc.sample_count,
        render_path.rtMain_render.desc.width, render_path.rtMain_render.desc.height,
        render_path.rtMain_render.desc.sample_count);
    const wi::graphics::Texture* depth = render_path.GetDepthStencil();
    if (depth == nullptr || !depth->IsValid()) {
        std::fprintf(stdout, "depth target MISSING\n");
    } else {
        std::fprintf(stdout, "depth %ux%u fmt=%d\n",
            depth->desc.width, depth->desc.height, (int)depth->desc.format);
    }
}

} // namespace probe
