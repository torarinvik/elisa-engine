constexpr size_t MAX_OVERLAY_IMAGES = 64;
constexpr unsigned IMAGE_SLOT_BITS = 7;
constexpr uint64_t IMAGE_SLOT_MASK = (uint64_t(1) << IMAGE_SLOT_BITS) - 1;
constexpr uint64_t MAX_IMAGE_GENERATION =
    uint64_t(std::numeric_limits<int64_t>::max()) >> IMAGE_SLOT_BITS;

// A screen-space textured quad. The resource keeps the atlas alive for the
// sprite; UV rectangles are stored in texels through Wicked's draw rect.
struct OverlayImageSlot {
    wi::Sprite sprite;
    wi::Resource resource;
    uint32_t texture_width = 0;
    uint32_t texture_height = 0;
    uint64_t generation = 0;
    bool live = false;
};
