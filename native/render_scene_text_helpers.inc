bool valid_overlay_text(const char* text, float x, float y, int32_t font_size,
    float red, float green, float blue, float alpha) {
    if (text == nullptr || !bounded(x, float(MAX_VIEWPORT)) || !bounded(y, float(MAX_VIEWPORT)) ||
        font_size < MIN_OVERLAY_FONT_SIZE || font_size > MAX_OVERLAY_FONT_SIZE ||
        !valid_color(red, green, blue, alpha)) return false;
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

// Well-formed UTF-8 without NUL, overlongs, surrogates or values past U+10FFFF.
bool valid_utf8_text(const uint8_t* bytes, size_t length) {
    size_t at = 0;
    while (at < length) {
        const uint8_t lead = bytes[at];
        if (lead == 0) return false;
        size_t extra = 0;
        uint32_t cp = 0;
        if (lead < 0x80) { at += 1; continue; }
        else if (lead >= 0xC2 && lead <= 0xDF) { extra = 1; cp = lead & 0x1F; }
        else if (lead >= 0xE0 && lead <= 0xEF) { extra = 2; cp = lead & 0x0F; }
        else if (lead >= 0xF0 && lead <= 0xF4) { extra = 3; cp = lead & 0x07; }
        else return false;
        if (length - at <= extra) return false;
        for (size_t k = 1; k <= extra; ++k) {
            const uint8_t next = bytes[at + k];
            if ((next & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (next & 0x3F);
        }
        if ((extra == 2 && (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF))) ||
            (extra == 3 && (cp < 0x10000 || cp > 0x10FFFF))) return false;
        at += extra + 1;
    }
    return true;
}

bool valid_overlay_text_prefix(const char* prefix) {
    if (prefix == nullptr) return false;
    for (size_t index = 0; index <= MAX_OVERLAY_TEXT_PREFIX_BYTES; ++index) {
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
    font.params.position = XMFLOAT3(x, y, OVERLAY_TEXT_DEPTH);
    font.params.size = font_size;
    font.params.color = wi::Color::fromFloat4(XMFLOAT4(red, green, blue, alpha));
    font.params.shadowColor = wi::Color(0, 0, 0, OVERLAY_TEXT_SHADOW_ALPHA);
    font.params.shadow_offset_x = OVERLAY_TEXT_SHADOW_OFFSET_X;
    font.params.shadow_offset_y = OVERLAY_TEXT_SHADOW_OFFSET_Y;
    font.SetHidden(false);
}
