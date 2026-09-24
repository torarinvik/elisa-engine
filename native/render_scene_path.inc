class ElisaRenderPath3D final : public wi::RenderPath3D {
public:
    explicit ElisaRenderPath3D(
        const std::array<ElectricArcSlot, MAX_ELECTRIC_ARCS>* arcs,
        probe::DebugDrawBridge* debug_draw, RenderSceneService* state)
        : arcs_(arcs), debug_draw_(debug_draw), state_(state) {}

    void Render() const override {
        if (state_ != nullptr) update_snapshot_lod_selection(*state_);
        if (arcs_ != nullptr) {
            for (const ElectricArcSlot& arc : *arcs_) {
                if (arc.live && arc.visible) {
                    wi::renderer::DrawTrailRuntime(&arc.halo);
                    wi::renderer::DrawTrailRuntime(&arc.core);
                    if (arc.branch.points.size() >= 2) {
                        wi::renderer::DrawTrailRuntime(&arc.branch);
                    }
                }
            }
        }
        if (debug_draw_ != nullptr && debug_draw_->pending() > 0) {
            debug_draw_->flush();
        }
#if defined(ELISA_RENDER_SCENE_TEST_PROBE)
        lod_gpu_timing_begin_render();
#endif
        wi::RenderPath3D::Render();
#if defined(ELISA_RENDER_SCENE_TEST_PROBE)
        lod_gpu_timing_end_render();
#endif
    }

private:
    const std::array<ElectricArcSlot, MAX_ELECTRIC_ARCS>* arcs_;
    probe::DebugDrawBridge* debug_draw_;
    RenderSceneService* state_;
};
