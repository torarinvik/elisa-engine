// Overlay widgets normally belong to the main render path. A secondary camera
// can own them instead, so they are drawn into that camera's local 2D target.
bool valid_camera_handle(const RenderSceneService& state, int64_t value, size_t& slot);

template <typename OverlaySlot>
wi::RenderPath2D* overlay_render_path(RenderSceneService& state, const OverlaySlot& overlay) {
    if (overlay.camera_slot < MAX_RENDER_CAMERAS) {
        const RenderCameraSlot& camera = state.cameras[overlay.camera_slot];
        if (camera.live && camera.generation == overlay.camera_generation &&
            camera.pipeline != nullptr) {
            return camera.pipeline.get();
        }
    }
    return state.path.get();
}

template <typename OverlaySlot, typename Add, typename Remove>
int32_t set_overlay_camera(RenderSceneService& state, OverlaySlot& overlay,
    int64_t camera_handle, Add add, Remove remove) {
    size_t target_slot = MAX_RENDER_CAMERAS;
    uint64_t target_generation = 0;
    if (camera_handle != 0) {
        if (!valid_camera_handle(state, camera_handle, target_slot)) {
            return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
        }
        RenderCameraSlot& camera = state.cameras[target_slot];
        if (camera.pipeline == nullptr || !camera.viewport_enabled) {
            return ELISA_RENDER_SCENE_BACKEND_FAILED;
        }
        target_generation = camera.generation;
    }

    wi::RenderPath2D* previous = overlay_render_path(state, overlay);
    wi::RenderPath2D* next = target_slot < MAX_RENDER_CAMERAS ?
        static_cast<wi::RenderPath2D*>(state.cameras[target_slot].pipeline.get()) :
        static_cast<wi::RenderPath2D*>(state.path.get());
    if (previous == nullptr || next == nullptr) return ELISA_RENDER_SCENE_BACKEND_FAILED;
    if (previous != next) {
        try {
            add(next);
        } catch (...) {
            return ELISA_RENDER_SCENE_BACKEND_FAILED;
        }
        remove(previous);
    }
    overlay.camera_slot = target_slot;
    overlay.camera_generation = target_generation;
    return ELISA_RENDER_SCENE_OK;
}

template <typename OverlaySlot, typename Add, typename Remove>
bool rehome_overlay_to_primary(RenderSceneService& state, OverlaySlot& overlay,
    size_t camera_slot, Add add, Remove remove) {
    if (!overlay.live || overlay.camera_slot != camera_slot) return true;
    wi::RenderPath2D* primary = state.path.get();
    wi::RenderPath2D* previous = overlay_render_path(state, overlay);
    if (primary == nullptr || previous == nullptr) return false;
    if (primary != previous) {
        try {
            add(primary);
        } catch (...) {
            return false;
        }
        remove(previous);
    }
    overlay.camera_slot = MAX_RENDER_CAMERAS;
    overlay.camera_generation = 0;
    return true;
}

int32_t rehome_camera_overlays_to_primary(RenderSceneService& state, size_t camera_slot) {
    for (OverlayTextSlot& overlay : state.overlay_texts) {
        if (!rehome_overlay_to_primary(state, overlay, camera_slot,
            [&](wi::RenderPath2D* path) { path->AddFont(&overlay.font); },
            [&](wi::RenderPath2D* path) { path->RemoveFont(&overlay.font); })) {
            return ELISA_RENDER_SCENE_BACKEND_FAILED;
        }
    }
    for (OverlayPanelSlot& overlay : state.overlay_panels) {
        if (!rehome_overlay_to_primary(state, overlay, camera_slot,
            [&](wi::RenderPath2D* path) { path->AddSprite(&overlay.sprite); },
            [&](wi::RenderPath2D* path) { path->RemoveSprite(&overlay.sprite); })) {
            return ELISA_RENDER_SCENE_BACKEND_FAILED;
        }
    }
    for (OverlayImageSlot& overlay : state.overlay_images) {
        if (!rehome_overlay_to_primary(state, overlay, camera_slot,
            [&](wi::RenderPath2D* path) { path->AddSprite(&overlay.sprite); },
            [&](wi::RenderPath2D* path) { path->RemoveSprite(&overlay.sprite); })) {
            return ELISA_RENDER_SCENE_BACKEND_FAILED;
        }
    }
    return ELISA_RENDER_SCENE_OK;
}
