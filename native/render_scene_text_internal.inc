constexpr size_t MAX_OVERLAY_TEXTS = 128;
constexpr unsigned TEXT_SLOT_BITS = 7;
constexpr uint64_t TEXT_SLOT_MASK = (uint64_t(1) << TEXT_SLOT_BITS) - 1;
constexpr uint64_t MAX_TEXT_GENERATION = uint64_t(std::numeric_limits<int64_t>::max()) >> TEXT_SLOT_BITS;
constexpr size_t MAX_OVERLAY_TEXT_BYTES = 256;
constexpr size_t OVERLAY_TEXT_BUFFER_BYTES = MAX_OVERLAY_TEXT_BYTES + 1;
constexpr uint64_t TEXT_SLOT_BASE = 1;
constexpr size_t MAX_SIGNED_I64_TEXT_BYTES = 20;
constexpr size_t MAX_OVERLAY_TEXT_PREFIX_BYTES = MAX_OVERLAY_TEXT_BYTES - MAX_SIGNED_I64_TEXT_BYTES;
constexpr int32_t MIN_OVERLAY_FONT_SIZE = 8;
constexpr int32_t MAX_OVERLAY_FONT_SIZE = 128;
constexpr float OVERLAY_TEXT_DEPTH = 0.0f;
constexpr int32_t OVERLAY_TEXT_SHADOW_ALPHA = 220;
constexpr float OVERLAY_TEXT_SHADOW_OFFSET_X = 1.0f;
constexpr float OVERLAY_TEXT_SHADOW_OFFSET_Y = 2.0f;

struct OverlayTextSlot {
    wi::SpriteFont font;
    uint64_t generation = 0;
    bool live = false;
};
