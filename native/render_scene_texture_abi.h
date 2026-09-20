#pragma once

// Included by render_scene_abi.cpp after its private scene-service helpers.
#include "render_scene_abi.h"
#include "wiResourceManager.h"
#include "wiScene.h"
#include "cooked_geometry_package.h"

#include <cstdio>
#include <exception>
#include <filesystem>
#include <utility>

namespace elisa::rendering {

inline int32_t assign_texture(wi::scene::Scene& scene, wi::ecs::Entity entity,
    int32_t slot, const char* asset_path) {
    static constexpr uint32_t texture_slots[4] = {
        wi::scene::MaterialComponent::BASECOLORMAP,
        wi::scene::MaterialComponent::NORMALMAP,
        wi::scene::MaterialComponent::SURFACEMAP,
        wi::scene::MaterialComponent::EMISSIVEMAP,
    };
    std::filesystem::path resolved_path;
    try {
        if (!elisa::assets::resolve_project_asset_path(asset_path, resolved_path)) {
            return ELISA_RENDER_SCENE_ASSET_LOAD_FAILED;
        }
        auto flags = wi::resourcemanager::Flags::IMPORT_BLOCK_COMPRESSED;
        if (slot == 1) flags |= wi::resourcemanager::Flags::IMPORT_NORMALMAP;
        wi::Resource resource = wi::resourcemanager::Load(resolved_path.string(), flags);
        if (!resource.IsValid() || !resource.GetTexture().IsValid()) {
            return ELISA_RENDER_SCENE_ASSET_LOAD_FAILED;
        }
        wi::scene::MaterialComponent* material = scene.materials.GetComponent(entity);
        if (material == nullptr) return ELISA_RENDER_SCENE_BACKEND_FAILED;
        wi::scene::MaterialComponent::TextureMap& texture =
            material->textures[texture_slots[slot]];
        texture.name = resolved_path.string();
        texture.resource = std::move(resource);
        material->SetDirty();
        return ELISA_RENDER_SCENE_OK;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Elisa render texture load failed: %s\n", error.what());
        return ELISA_RENDER_SCENE_ASSET_LOAD_FAILED;
    } catch (...) {
        return ELISA_RENDER_SCENE_ASSET_LOAD_FAILED;
    }
}

} // namespace elisa::rendering

extern "C" int32_t elisa_render_scene_v1_set_texture(
    int64_t handle, int32_t slot, const char* asset_path) {
    if (slot < 0 || slot >= 4) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    size_t instance_slot = MAX_INSTANCES;
    if (!valid_handle(state, handle, instance_slot)) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    return elisa::rendering::assign_texture(*state.scene,
        state.instances[instance_slot].entity, slot, asset_path);
}
