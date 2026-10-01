#include <cfloat>
// R06: world bounds of a render instance, read from Wicked's per-object AABBs
// (the instance entity and any imported mesh entities it owns) and mapped back
// to Elisa coordinates through from_wicked. Bounds follow the last scene update.
namespace {

bool render_object_bounds(const wi::scene::Scene& scene, wi::ecs::Entity entity,
    XMFLOAT3& low, XMFLOAT3& high) {
    const size_t index = scene.objects.GetIndex(entity);
    if (index == ~0ull || index >= scene.aabb_objects.size()) return false;
    const wi::primitive::AABB& box = scene.aabb_objects[index];
    for (int corner = 0; corner < 8; ++corner) {
        const XMFLOAT3 elisa = probe::coordinates::from_wicked(XMFLOAT3(
            (corner & 1) ? box._max.x : box._min.x, (corner & 2) ? box._max.y : box._min.y,
            (corner & 4) ? box._max.z : box._min.z));
        low = XMFLOAT3(std::fmin(low.x, elisa.x), std::fmin(low.y, elisa.y), std::fmin(low.z, elisa.z));
        high = XMFLOAT3(std::fmax(high.x, elisa.x), std::fmax(high.y, elisa.y), std::fmax(high.z, elisa.z));
    }
    return true;
}

} // namespace

extern "C" int32_t elisa_render_scene_v1_instance_bounds(int64_t handle,
    float* min_x, float* min_y, float* min_z, float* max_x, float* max_y, float* max_z) {
    if (min_x == nullptr || min_y == nullptr || min_z == nullptr ||
        max_x == nullptr || max_y == nullptr || max_z == nullptr) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    if (state.scene == nullptr) return ELISA_RENDER_SCENE_BACKEND_FAILED;
    size_t slot = MAX_INSTANCES;
    if (!valid_handle(state, handle, slot)) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    const InstanceSlot& instance = state.instances[slot];
    XMFLOAT3 low(FLT_MAX, FLT_MAX, FLT_MAX);
    XMFLOAT3 high(-FLT_MAX, -FLT_MAX, -FLT_MAX);
    bool any = render_object_bounds(*state.scene, instance.entity, low, high);
    for (wi::ecs::Entity entity : instance.imported_mesh_entities) {
        any = render_object_bounds(*state.scene, entity, low, high) || any;
    }
    if (!any) return ELISA_RENDER_SCENE_BACKEND_FAILED;
    *min_x = low.x; *min_y = low.y; *min_z = low.z;
    *max_x = high.x; *max_y = high.y; *max_z = high.z;
    return ELISA_RENDER_SCENE_OK;
}
