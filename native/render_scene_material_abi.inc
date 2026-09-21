// Private material ABI implementations included exactly once by render_scene_abi.cpp.
extern "C" int32_t elisa_render_scene_v1_set_color(
    int64_t handle, float red, float green, float blue, float alpha) {
    if (!valid_color(red, green, blue, alpha)) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    size_t slot = MAX_INSTANCES;
    if (!valid_handle(state, handle, slot)) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    wi::scene::MaterialComponent* material = state.scene->materials.GetComponent(state.instances[slot].entity);
    if (material == nullptr) return ELISA_RENDER_SCENE_BACKEND_FAILED;
    material->SetBaseColor(XMFLOAT4(red, green, blue, alpha));
    material->userBlendMode = alpha < 0.999f ? wi::enums::BLENDMODE_ALPHA : wi::enums::BLENDMODE_OPAQUE;
    return ELISA_RENDER_SCENE_OK;
}

extern "C" int32_t elisa_render_scene_v1_set_texture(
    int64_t handle, int32_t slot, const char* asset_path) {
    if (!elisa::rendering::textures::valid_slot(slot)) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    size_t instance_slot = MAX_INSTANCES;
    if (!valid_handle(state, handle, instance_slot)) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;

    wi::scene::MaterialComponent* material =
        state.scene->materials.GetComponent(state.instances[instance_slot].entity);
    if (material == nullptr) return ELISA_RENDER_SCENE_BACKEND_FAILED;
    try {
        return elisa::rendering::textures::assign_project_texture(*material, slot, asset_path)
            ? ELISA_RENDER_SCENE_OK : ELISA_RENDER_SCENE_ASSET_LOAD_FAILED;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Elisa render texture load failed: %s\n", error.what());
        return ELISA_RENDER_SCENE_ASSET_LOAD_FAILED;
    } catch (...) {
        return ELISA_RENDER_SCENE_ASSET_LOAD_FAILED;
    }
}

extern "C" int32_t elisa_render_scene_v1_set_emissive(
    int64_t handle, float red, float green, float blue, float strength) {
    if (!elisa::render_scene_effects::valid_emission(red, green, blue, strength)) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    size_t slot = MAX_INSTANCES;
    if (!valid_handle(state, handle, slot)) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    wi::scene::MaterialComponent* material = state.scene->materials.GetComponent(state.instances[slot].entity);
    if (material == nullptr) return ELISA_RENDER_SCENE_BACKEND_FAILED;
    elisa::render_scene_effects::apply_emission(*material, red, green, blue, strength);
    return ELISA_RENDER_SCENE_OK;
}

// Opt-in lighting keeps legacy unlit scene clients compatible.
extern "C" int32_t elisa_render_scene_v1_set_lit_material(int64_t handle, float roughness, float metallic) {
    if (!finite(roughness) || !finite(metallic) || roughness < 0 || roughness > 1 || metallic < 0 || metallic > 1)
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    size_t slot = MAX_INSTANCES;
    if (!valid_handle(state, handle, slot)) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    auto* material = state.scene->materials.GetComponent(state.instances[slot].entity);
    auto* object = state.scene->objects.GetComponent(state.instances[slot].entity);
    if (!material || !object) return ELISA_RENDER_SCENE_BACKEND_FAILED;
    material->shaderType = wi::scene::MaterialComponent::SHADERTYPE_PBR;
    material->SetRoughness(roughness);
    material->SetMetalness(metallic);
    material->SetCastShadow(true);
    material->SetDirty();
    object->SetCastShadow(true);
    return ELISA_RENDER_SCENE_OK;
}

extern "C" int32_t elisa_render_scene_v1_set_texture_uv_transform(int64_t handle,
    float scale_u, float scale_v, float offset_u, float offset_v) {
    if (!finite(scale_u) || !finite(scale_v) || !finite(offset_u) || !finite(offset_v))
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    size_t slot = MAX_INSTANCES;
    if (!valid_handle(state, handle, slot)) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    auto* material = state.scene->materials.GetComponent(state.instances[slot].entity);
    if (!material) return ELISA_RENDER_SCENE_BACKEND_FAILED;
    material->texMulAdd = XMFLOAT4(scale_u, scale_v, offset_u, offset_v);
    material->SetDirty();
    return ELISA_RENDER_SCENE_OK;
}
