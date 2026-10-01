// Private streamed-mesh ABI included exactly once by render_scene_abi.cpp.
// A streamed mesh is built from the cooked bytes the native asset stream has
// already decoded, and its material samples the GPU texture the stream
// uploaded, so no package is reread from disk and the instance shares the
// stream's resident texture until the instance is destroyed.
#include "asset_stream_borrow.h"

extern "C" int64_t elisa_render_scene_v1_create_streamed_mesh(
    int64_t mesh_stream, int64_t material_stream,
    float px, float py, float pz,
    float qx, float qy, float qz, float qw,
    float sx, float sy, float sz) {
    if (!valid_transform(px, py, pz, qx, qy, qz, qw, sx, sy, sz))
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    // Borrow from the asset stream before the render lock; the stream takes
    // its own lock and never calls back into the render scene.
    std::vector<uint8_t> bytes;
    wi::graphics::Texture base_map;
    if (!elisa_asset_stream_copy_bytes(mesh_stream, bytes) ||
        !elisa_asset_stream_share_texture(material_stream, base_map) || !base_map.IsValid())
        return ELISA_RENDER_SCENE_ASSET_LOAD_FAILED;
    elisa::assets::CookedGeometry geometry;
    std::string load_error;
    try {
        if (!elisa::assets::load_cooked_geometry_bytes(bytes.data(), bytes.size(), geometry, load_error)) {
            std::fprintf(stderr, "Elisa streamed mesh decode failed: %s\n", load_error.c_str());
            return ELISA_RENDER_SCENE_ASSET_LOAD_FAILED;
        }
    } catch (...) {
        return ELISA_RENDER_SCENE_ASSET_LOAD_FAILED;
    }
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    // White base colour: the visible colour comes from the bound texture.
    return install_cooked_mesh(state, geometry, px, py, pz, qx, qy, qz, qw, sx, sy, sz,
        1.0f, 1.0f, 1.0f, 1.0f, &base_map);
}

// 1 when the instance's material samples exactly the GPU texture the stream
// keeps resident for `material_stream` and its base colour is white, 0 when
// it does not, or a negative status.
extern "C" int32_t elisa_render_scene_v1_streamed_material_bound(
    int64_t handle, int64_t material_stream) {
    wi::graphics::Texture expected;
    if (!elisa_asset_stream_share_texture(material_stream, expected) || !expected.IsValid())
        return 0;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    size_t slot = MAX_INSTANCES;
    if (!valid_handle(state, handle, slot)) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    const wi::scene::MaterialComponent* material =
        state.scene->materials.GetComponent(state.instances[slot].entity);
    if (material == nullptr) return ELISA_RENDER_SCENE_BACKEND_FAILED;
    const wi::Resource& resource =
        material->textures[wi::scene::MaterialComponent::BASECOLORMAP].resource;
    if (!resource.IsValid() || !resource.GetTexture().IsValid()) return 0;
    const bool same = resource.GetTexture().internal_state.get() == expected.internal_state.get();
    const XMFLOAT4 base = material->baseColor;
    const bool white = base.x == 1.0f && base.y == 1.0f && base.z == 1.0f;
    return same && white ? 1 : 0;
}
