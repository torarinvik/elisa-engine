constexpr size_t MAX_OVERLAY_TEXTS = 32;
constexpr unsigned TEXT_SLOT_BITS = 6;
constexpr uint64_t TEXT_SLOT_MASK = (uint64_t(1) << TEXT_SLOT_BITS) - 1;
constexpr uint64_t MAX_TEXT_GENERATION = uint64_t(std::numeric_limits<int64_t>::max()) >> TEXT_SLOT_BITS;
constexpr size_t MAX_OVERLAY_TEXT_BYTES = 256;

struct OverlayTextSlot {
    wi::SpriteFont font;
    uint64_t generation = 0;
    bool live = false;
};
