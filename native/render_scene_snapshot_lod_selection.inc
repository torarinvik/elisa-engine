void update_snapshot_lod_selection(RenderSceneService& state) {
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state) || state.snapshot_transaction_active ||
        state.scene == nullptr || state.camera == nullptr || state.height <= 0) return;
    const XMVECTOR camera_eye = state.camera->GetEye();
    const XMVECTOR camera_at = state.camera->GetAt();
    for (size_t slot = 0; slot < MAX_INSTANCES; ++slot) {
        InstanceSlot& instance = state.instances[slot];
        if (!instance.live || (instance.mesh_high == 0 && instance.mesh_low == 0)) continue;
        const size_t asset_slot = snapshot_mesh_asset_slot(state, instance.mesh_high, instance.mesh_low);
        if (asset_slot == MAX_SNAPSHOT_MESH_ASSETS) continue;
        const SnapshotMeshAssetSlot& asset = state.snapshot_mesh_assets[asset_slot];
        if (asset.lod_level_count < 2 || instance.snapshot_lod_level >= asset.lod_level_count) continue;
        const wi::scene::ObjectComponent* object = state.scene->objects.GetComponent(instance.entity);
        const wi::scene::TransformComponent* transform = state.scene->transforms.GetComponent(instance.entity);
        if (object == nullptr || transform == nullptr || !snapshot_instance_visible(state, instance)) continue;

        const XMFLOAT3 position = transform->GetPosition();
        const XMFLOAT3 scale = transform->GetScale();
        const double world_scale = std::max({std::abs(double(scale.x)),
            std::abs(double(scale.y)), std::abs(double(scale.z))});
        const XMVECTOR offset = XMVectorSubtract(XMLoadFloat3(&position), camera_eye);
        const double depth = XMVectorGetX(XMVector3Dot(offset, camera_at));
        const double radius = asset.lod_manifest.object_extent * world_scale * 0.8660254037844386;
        const double nearest_depth = std::max(double(state.camera->zNearP), depth - radius);
        double projection_scale = 0.0;
        if (state.camera->IsOrtho()) {
            projection_scale = double(state.height) / state.camera->ortho_vertical_size;
        } else {
            const double denominator = 2.0 * std::tan(double(state.camera->fov) * 0.5) * nearest_depth;
            projection_scale = double(state.height) / denominator;
        }
        const double pixel_budget = std::exp2(double(object->lod_bias));
        const size_t selected = elisa::assets::select_lod_level(asset.lod_manifest, world_scale,
            projection_scale, pixel_budget, instance.snapshot_lod_level);
        if (selected < asset.lod_level_count && selected != instance.snapshot_lod_level) {
            (void)set_snapshot_mesh_lod_level_unlocked(state, slot, selected);
        }
    }
}
