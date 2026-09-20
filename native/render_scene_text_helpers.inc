bool valid_overlay_text(const char* text, float x, float y, int32_t font_size,
    float red, float green, float blue, float alpha) {
    if (text == nullptr || !bounded(x, float(MAX_VIEWPORT)) || !bounded(y, float(MAX_VIEWPORT)) ||
        font_size < 8 || font_size > 128 || !valid_color(red, green, blue, alpha)) return false;
    for (size_t index = 0; index <= MAX_OVERLAY_TEXT_BYTES; ++index) {
        if (text[index] == '\0') return true;
    }
    return false;
}

bool valid_overlay_text_value(const char* text) {
    if (text == nullptr) return false;
    for (size_t index = 0; index <= MAX_OVERLAY_TEXT_BYTES; ++index) {
        if (text[index] == '\0') return true;
    }
    return false;
}

bool valid_overlay_text_prefix(const char* prefix) {
    if (prefix == nullptr) return false;
    // A signed 64-bit decimal value needs at most 20 bytes.
    for (size_t index = 0; index <= MAX_OVERLAY_TEXT_BYTES - 20; ++index) {
        if (prefix[index] == '\0') return true;
    }
    return false;
}

size_t find_free_text_slot(const RenderSceneService& state) {
    for (size_t index = 0; index < MAX_OVERLAY_TEXTS; ++index) {
        if (!state.overlay_texts[index].live) return index;
    }
    return MAX_OVERLAY_TEXTS;
}

size_t decode_text_handle(const RenderSceneService& state, int64_t handle) {
    if (handle <= 0) return MAX_OVERLAY_TEXTS;
    const uint64_t value = uint64_t(handle);
    const uint64_t encoded_slot = value & TEXT_SLOT_MASK;
    if (encoded_slot == 0 || encoded_slot > MAX_OVERLAY_TEXTS) return MAX_OVERLAY_TEXTS;
    const size_t slot = size_t(encoded_slot - 1);
    const uint64_t generation = value >> TEXT_SLOT_BITS;
    const OverlayTextSlot& text = state.overlay_texts[slot];
    return generation != 0 && text.live && text.generation == generation ? slot : MAX_OVERLAY_TEXTS;
}

void apply_overlay_text(wi::SpriteFont& font, const char* text, float x, float y,
    int32_t font_size, float red, float green, float blue, float alpha) {
    font.SetText(text);
    font.params.position = XMFLOAT3(x, y, 0.0f);
    font.params.size = font_size;
    font.params.color = wi::Color::fromFloat4(XMFLOAT4(red, green, blue, alpha));
    font.params.shadowColor = wi::Color(0, 0, 0, 220);
    font.params.shadow_offset_x = 1.0f;
    font.params.shadow_offset_y = 2.0f;
    font.SetHidden(false);
}
