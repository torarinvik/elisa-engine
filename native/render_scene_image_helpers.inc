bool valid_overlay_image_rect(float x, float y, float width, float height) {
    return bounded(x, float(MAX_VIEWPORT)) && bounded(y, float(MAX_VIEWPORT)) &&
        bounded(width, float(MAX_VIEWPORT)) && bounded(height, float(MAX_VIEWPORT)) &&
        width > 0.0f && height > 0.0f;
}

bool valid_overlay_image_uv(float u0, float v0, float u1, float v1) {
    return finite(u0) && finite(v0) && finite(u1) && finite(v1) &&
        u0 >= 0.0f && v0 >= 0.0f && u1 <= 1.0f && v1 <= 1.0f && u0 < u1 && v0 < v1;
}

size_t find_free_image_slot(const RenderSceneService& state) {
    for (size_t index = 0; index < MAX_OVERLAY_IMAGES; ++index) {
        if (!state.overlay_images[index].live) return index;
    }
    return MAX_OVERLAY_IMAGES;
}

size_t decode_image_handle(const RenderSceneService& state, int64_t handle) {
    if (handle <= 0) return MAX_OVERLAY_IMAGES;
    const uint64_t value = uint64_t(handle);
    const uint64_t encoded_slot = value & IMAGE_SLOT_MASK;
    if (encoded_slot == 0 || encoded_slot > MAX_OVERLAY_IMAGES) return MAX_OVERLAY_IMAGES;
    const size_t slot = size_t(encoded_slot - 1);
    const uint64_t generation = value >> IMAGE_SLOT_BITS;
    const OverlayImageSlot& image = state.overlay_images[slot];
    return generation != 0 && image.live && image.generation == generation ? slot : MAX_OVERLAY_IMAGES;
}

void apply_overlay_image_uv(OverlayImageSlot& entry, float u0, float v0, float u1, float v1) {
    const float width = float(entry.texture_width);
    const float height = float(entry.texture_height);
    entry.sprite.params.enableDrawRect(XMFLOAT4(u0 * width, v0 * height,
        (u1 - u0) * width, (v1 - v0) * height));
}

// Loads the atlas through Wicked's resource manager; the project root bounds
// the path exactly like material textures.
bool apply_overlay_image(OverlayImageSlot& entry, const char* asset_path,
    float x, float y, float width, float height) {
    std::filesystem::path resolved;
    if (!elisa::assets::resolve_project_asset_path(asset_path, resolved)) return false;
    wi::Resource resource = wi::resourcemanager::Load(resolved.string(),
        wi::resourcemanager::Flags::NONE);
    if (!resource.IsValid() || !resource.GetTexture().IsValid()) return false;
    const wi::graphics::TextureDesc& desc = resource.GetTexture().GetDesc();
    if (desc.width == 0 || desc.height == 0) return false;
    entry.resource = std::move(resource);
    entry.texture_width = desc.width;
    entry.texture_height = desc.height;
    entry.sprite.textureResource = entry.resource;
    entry.sprite.params = wi::image::Params();
    entry.sprite.params.pos = XMFLOAT3(x, y, 0.0f);
    entry.sprite.params.siz = XMFLOAT2(width, height);
    entry.sprite.params.color = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    entry.sprite.params.opacity = 1.0f;
    entry.sprite.params.sampleFlag = wi::image::SAMPLEMODE_CLAMP;
    entry.sprite.params.blendFlag = wi::enums::BLENDMODE_ALPHA;
    apply_overlay_image_uv(entry, 0.0f, 0.0f, 1.0f, 1.0f);
    entry.sprite.SetHidden(false);
    return true;
}

void release_overlay_image(RenderSceneService& state, OverlayImageSlot& entry) {
    if (state.path != nullptr) state.path->RemoveSprite(&entry.sprite);
    entry.sprite.SetHidden(true);
    entry.sprite.textureResource = {};
    entry.resource = {};
    entry.texture_width = 0;
    entry.texture_height = 0;
    entry.live = false;
}
