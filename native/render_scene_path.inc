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

    void Compose(wi::graphics::CommandList cmd) const override {
        wi::RenderPath3D::Compose(cmd);
        if (state_ == nullptr) return;

        wi::image::Params image;
        image.blendFlag = wi::enums::BLENDMODE_OPAQUE;
        image.quality = wi::image::QUALITY_LINEAR;
        image.pivot = XMFLOAT2(0.0f, 0.0f);

        size_t composed = 0;
        std::lock_guard<std::mutex> guard(state_->mutex);
        for (const RenderCameraSlot& slot : state_->cameras) {
            if (!slot.live || !slot.viewport_enabled || slot.viewport_width <= 0 ||
                slot.viewport_height <= 0 || state_->scene == nullptr) continue;
            wi::scene::CameraComponent* camera = state_->scene->cameras.GetComponent(slot.entity);
            if (camera == nullptr) continue;
            const wi::graphics::Texture& target = camera->render_to_texture.rendertarget_render.IsValid() ?
                camera->render_to_texture.rendertarget_render : camera->render_to_texture.rendertarget_display;
            if (!target.IsValid()) continue;

            image.pos = XMFLOAT3(PhysicalToLogical(slot.viewport_x),
                PhysicalToLogical(slot.viewport_y), 0.0f);
            image.siz = XMFLOAT2(PhysicalToLogical(slot.viewport_width),
                PhysicalToLogical(slot.viewport_height));
            wi::image::Draw(&target, image, cmd);
            ++composed;
        }
#if defined(ELISA_RENDER_SCENE_TEST_PROBE)
        state_->camera_viewports_composed = composed;
#endif
    }

private:
    const std::array<ElectricArcSlot, MAX_ELECTRIC_ARCS>* arcs_;
    probe::DebugDrawBridge* debug_draw_;
    RenderSceneService* state_;
};
