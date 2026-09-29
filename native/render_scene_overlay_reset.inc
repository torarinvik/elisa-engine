void reset_overlay_slots(RenderSceneService& state) {
    for (OverlayTextSlot& text : state.overlay_texts) {
        text.font.SetHidden(true);
        try { text.font.SetText(""); } catch (...) {}
        text.camera_slot = MAX_RENDER_CAMERAS;
        text.camera_generation = 0;
        text.live = false;
    }
    for (OverlayPanelSlot& panel : state.overlay_panels) {
        panel.sprite.SetHidden(true);
        panel.camera_slot = MAX_RENDER_CAMERAS;
        panel.camera_generation = 0;
        panel.live = false;
    }
    for (OverlayImageSlot& image : state.overlay_images) {
        image.sprite.SetHidden(true);
        image.sprite.textureResource = {};
        image.resource = {};
        image.camera_slot = MAX_RENDER_CAMERAS;
        image.camera_generation = 0;
        image.live = false;
    }
}
