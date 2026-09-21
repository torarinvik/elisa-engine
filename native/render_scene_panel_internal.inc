constexpr size_t MAX_OVERLAY_PANELS = 64;
constexpr unsigned PANEL_SLOT_BITS = 7;
constexpr uint64_t PANEL_SLOT_MASK = (uint64_t(1) << PANEL_SLOT_BITS) - 1;
constexpr uint64_t MAX_PANEL_GENERATION =
    uint64_t(std::numeric_limits<int64_t>::max()) >> PANEL_SLOT_BITS;

struct OverlayPanelSlot {
    wi::Sprite sprite;
    uint64_t generation = 0;
    bool live = false;
};
