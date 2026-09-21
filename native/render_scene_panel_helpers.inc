bool valid_overlay_panel(float x, float y, float width, float height,
    float red, float green, float blue, float alpha) {
    return bounded(x, float(MAX_VIEWPORT)) && bounded(y, float(MAX_VIEWPORT)) &&
        bounded(width, float(MAX_VIEWPORT)) && bounded(height, float(MAX_VIEWPORT)) &&
        width > 0.0f && height > 0.0f && valid_color(red, green, blue, alpha);
}

size_t find_free_panel_slot(const RenderSceneService& state) {
    for (size_t index = 0; index < MAX_OVERLAY_PANELS; ++index) {
        if (!state.overlay_panels[index].live) return index;
    }
    return MAX_OVERLAY_PANELS;
}

size_t decode_panel_handle(const RenderSceneService& state, int64_t handle) {
    if (handle <= 0) return MAX_OVERLAY_PANELS;
    const uint64_t value = uint64_t(handle);
    const uint64_t encoded_slot = value & PANEL_SLOT_MASK;
    if (encoded_slot == 0 || encoded_slot > MAX_OVERLAY_PANELS) return MAX_OVERLAY_PANELS;
    const size_t slot = size_t(encoded_slot - 1);
    const uint64_t generation = value >> PANEL_SLOT_BITS;
    const OverlayPanelSlot& panel = state.overlay_panels[slot];
    return generation != 0 && panel.live && panel.generation == generation ? slot : MAX_OVERLAY_PANELS;
}

void apply_overlay_panel(wi::Sprite& sprite, float x, float y,
    float width, float height, float red, float green, float blue, float alpha) {
    sprite.textureResource.SetTexture(*wi::texturehelper::getWhite());
    sprite.params.pos = XMFLOAT3(x, y, 0.0f);
    sprite.params.siz = XMFLOAT2(width, height);
    sprite.params.color = XMFLOAT4(red, green, blue, alpha);
    sprite.params.opacity = 1.0f;
    sprite.SetHidden(false);
}
