class ElisaRenderPath3D final : public wi::RenderPath3D {
public:
    explicit ElisaRenderPath3D(
        const std::array<ElectricArcSlot, MAX_ELECTRIC_ARCS>* arcs,
        probe::DebugDrawBridge* debug_draw, RenderSceneService* state)
        : arcs_(arcs), debug_draw_(debug_draw), state_(state) {}

    void PreUpdate() override {
        wi::RenderPath3D::PreUpdate();
        if (state_ == nullptr || state_->scene == nullptr) return;
        for (RenderCameraSlot& slot : state_->cameras) {
            if (!slot.viewport_enabled || slot.pipeline == nullptr) continue;
            slot.pipeline->scene = state_->scene.get();
            slot.pipeline->camera = state_->scene->cameras.GetComponent(slot.entity);
            if (slot.pipeline->camera != nullptr) slot.pipeline->PreUpdate();
        }
    }

    void Update(float dt) override {
        wi::RenderPath3D::Update(dt);
        if (state_ == nullptr || state_->scene == nullptr) return;
        const float elapsed = std::max(dt, 0.0f);
        for (RenderCameraSlot& slot : state_->cameras) {
            if (!slot.viewport_enabled || slot.pipeline == nullptr) continue;
            slot.pipeline->scene = state_->scene.get();
            slot.pipeline->camera = state_->scene->cameras.GetComponent(slot.entity);
            if (slot.pipeline->camera == nullptr) continue;
            if (!slot.rendered || slot.update_interval <= 0.0f) {
                slot.render_pending = true;
                slot.update_elapsed = 0.0f;
            } else {
                slot.update_elapsed += elapsed;
                if (slot.update_elapsed >= slot.update_interval) {
                    slot.update_elapsed = std::fmod(slot.update_elapsed, slot.update_interval);
                    slot.render_pending = true;
                }
            }
            if (slot.render_pending) slot.pipeline->Update(dt);
        }
    }

    void PreRender() override {
        wi::RenderPath3D::PreRender();
        if (state_ == nullptr || state_->scene == nullptr) return;
        for (RenderCameraSlot& slot : state_->cameras) {
            if (!slot.viewport_enabled || !slot.render_pending || slot.pipeline == nullptr ||
                slot.pipeline->camera == nullptr) continue;
            const CameraQualityGlobals globals = apply_camera_quality_globals(*state_, slot);
            slot.pipeline->PreRender();
            restore_camera_quality_globals(*state_, globals);
        }
    }

    void Render() const override {
        if (state_ != nullptr) update_snapshot_lod_selection(*state_);
        if (state_ != nullptr && state_->scene != nullptr) {
            for (RenderCameraSlot& slot : state_->cameras) {
                if (!slot.viewport_enabled || !slot.render_pending || slot.pipeline == nullptr ||
                    slot.pipeline->camera == nullptr) continue;
                const CameraQualityGlobals globals = apply_camera_quality_globals(*state_, slot);
                slot.pipeline->Render();
                restore_camera_quality_globals(*state_, globals);
                slot.render_pending = false;
                slot.rendered = slot.pipeline->GetRenderResult3D().IsValid();
            }
        }
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
                slot.viewport_height <= 0 || slot.pipeline == nullptr) continue;
            const wi::graphics::Texture& target = slot.pipeline->GetRenderResult3D();
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
    struct CameraQualityGlobals {
        bool temporal_aa;
        bool fog;
        int32_t shadow_quality;
        float sun_shadow_receiver_bias;
    };

    static CameraQualityGlobals apply_camera_quality_globals(
        RenderSceneService& state, const RenderCameraSlot& slot) {
        CameraQualityGlobals previous{wi::renderer::GetTemporalAAEnabled(),
            state.scene != nullptr && state.scene->weather.IsHeightFog(),
            state.shadow_quality, state.sun_shadow_receiver_bias};
        if (slot.quality_profile_set) {
            wi::renderer::SetTemporalAAEnabled(slot.profile_temporal_aa);
            if (state.scene != nullptr) state.scene->weather.SetHeightFog(slot.profile_fog);
            elisa::render_scene_effects::apply_shadow_quality(slot.profile_shadow_quality);
            if (state.scene != nullptr && state.sun_entity != wi::ecs::INVALID_ENTITY) {
                wi::scene::LightComponent* sun = state.scene->lights.GetComponent(state.sun_entity);
                if (sun != nullptr && sun->type == wi::scene::LightComponent::DIRECTIONAL) {
                    sun->shadow_bias = slot.profile_sun_shadow_receiver_bias;
                }
            }
        }
        return previous;
    }

    static void restore_camera_quality_globals(
        RenderSceneService& state, const CameraQualityGlobals& previous) {
        wi::renderer::SetTemporalAAEnabled(previous.temporal_aa);
        if (state.scene != nullptr) state.scene->weather.SetHeightFog(previous.fog);
        elisa::render_scene_effects::apply_shadow_quality(previous.shadow_quality);
        if (state.scene != nullptr && state.sun_entity != wi::ecs::INVALID_ENTITY) {
            wi::scene::LightComponent* sun = state.scene->lights.GetComponent(state.sun_entity);
            if (sun != nullptr && sun->type == wi::scene::LightComponent::DIRECTIONAL) {
                sun->shadow_bias = previous.sun_shadow_receiver_bias;
            }
        }
    }

    const std::array<ElectricArcSlot, MAX_ELECTRIC_ARCS>* arcs_;
    probe::DebugDrawBridge* debug_draw_;
    RenderSceneService* state_;
};
