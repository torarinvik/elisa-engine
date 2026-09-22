void reset_overlay_slots(RenderSceneService& state) {
    for (OverlayTextSlot& text : state.overlay_texts) {
        text.font.SetHidden(true);
        try { text.font.SetText(""); } catch (...) {}
        text.live = false;
    }
    for (OverlayPanelSlot& panel : state.overlay_panels) {
        panel.sprite.SetHidden(true);
        panel.live = false;
    }
    for (OverlayImageSlot& image : state.overlay_images) {
        image.sprite.SetHidden(true);
        image.sprite.textureResource = {};
        image.resource = {};
        image.live = false;
    }
}
