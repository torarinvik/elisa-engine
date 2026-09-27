    void execute(const wi::RenderPath3D& path, wi::graphics::CommandList command_list) {
        std::lock_guard<std::mutex> guard(mutex_);
        if (!active_.valid) return;
#if defined(ELISA_RENDER_SCENE_TEST_PROBE)
        last_fallback_preserved_ = false;
#endif
        const wi::graphics::Texture* imported = path.GetLastPostprocessRT();
        if (imported == nullptr || !imported->IsValid() || scene_color_import_index(active_) == NO_TARGET) {
            last_status_ = BACKEND_FAILED;
            return;
        }
        const XMUINT2 resolution = path.GetInternalResolution();
#if defined(ELISA_RENDER_SCENE_TEST_PROBE)
        const bool suspend = suspend_next_frame_;
        suspend_next_frame_ = false;
#else
        constexpr bool suspend = false;
#endif
        if (suspend || resolution.x == 0 || resolution.y == 0) {
            last_status_ = SUSPENDED;
#if defined(ELISA_RENDER_SCENE_TEST_PROBE)
            last_fallback_preserved_ = path.GetLastPostprocessRT() == imported;
#endif
            return;
        }
        if (targets_dirty_ || internal_width_ != resolution.x || internal_height_ != resolution.y) {
            if (!prepare_targets(path, resolution.x, resolution.y)) {
                last_status_ = BACKEND_FAILED;
#if defined(ELISA_RENDER_SCENE_TEST_PROBE)
                last_fallback_preserved_ = path.GetLastPostprocessRT() == imported;
#endif
                return;
            }
        }
        for (uint32_t index = 0; index < active_.pass_count; ++index) {
#if defined(ELISA_RENDER_SCENE_TEST_PROBE)
            if (fail_pass_index_ == index) {
                fail_pass_index_ = NO_TARGET;
                last_status_ = BACKEND_FAILED;
                last_fallback_preserved_ = path.GetLastPostprocessRT() == imported;
                return;
            }
#endif
            const Pass& pass = active_.passes[index];
            wi::graphics::Texture* destination = texture_for(pass.destination_id, path);
            if (destination == nullptr) {
                last_status_ = BACKEND_FAILED;
                return;
            }
            if (pass.operation == Operation::ClearColor) {
                wi::graphics::GraphicsDevice* device = wi::graphics::GetDevice();
                if (device == nullptr || destination->GetDesc().format == native_format(Format::Depth32)) {
                    last_status_ = BACKEND_FAILED;
                    return;
                }
                device->RenderPassBegin(destination, command_list, true);
                device->RenderPassEnd(command_list);
            } else if (pass.operation == Operation::ClearDepth) {
                wi::graphics::GraphicsDevice* device = wi::graphics::GetDevice();
                if (device == nullptr || destination->GetDesc().format != native_format(Format::Depth32)) {
                    last_status_ = BACKEND_FAILED;
                    return;
                }
                const wi::graphics::RenderPassImage depth =
                    wi::graphics::RenderPassImage::DepthStencil(destination,
                        wi::graphics::RenderPassImage::LoadOp::CLEAR);
                device->RenderPassBegin(&depth, 1, command_list);
                device->RenderPassEnd(command_list);
#if defined(ELISA_RENDER_SCENE_TEST_PROBE)
                ++depth_clear_count_;
#endif
            } else {
                wi::graphics::Texture* source = texture_for(pass.source_id, path);
                if (source == nullptr || source == destination ||
                    source->GetDesc().width != destination->GetDesc().width ||
                    source->GetDesc().height != destination->GetDesc().height) {
                    last_status_ = INVALID_ARGUMENT;
                    return;
                }
                if (pass.operation == Operation::VisualizeDepth) {
                    if (source->GetDesc().format != native_format(Format::R32Float) ||
                        destination->GetDesc().format == native_format(Format::Depth32) ||
                        source->GetDesc().sample_count != 1 || destination->GetDesc().sample_count != 1) {
                        last_status_ = INVALID_ARGUMENT;
                        return;
                    }
                    if (!visualize_depth(*source, *destination, command_list)) {
                        last_status_ = BACKEND_FAILED;
                        return;
                    }
#if defined(ELISA_RENDER_SCENE_TEST_PROBE)
                    ++scene_depth_read_count_;
#endif
                } else if (pass.operation == Operation::BlendColor) {
                    if (source->GetDesc().format == native_format(Format::Depth32) ||
                        destination->GetDesc().format == native_format(Format::Depth32) ||
                        source->GetDesc().format == native_format(Format::R32Float) ||
                        destination->GetDesc().format == native_format(Format::R32Float) ||
                        source->GetDesc().sample_count != 1 || destination->GetDesc().sample_count != 1) {
                        last_status_ = INVALID_ARGUMENT;
                        return;
                    }
                    if (!blend_color(*source, *destination, pass.opacity, command_list)) {
                        last_status_ = BACKEND_FAILED;
                        return;
                    }
                } else if (pass.operation == Operation::AdjustSaturation) {
                    if (source->GetDesc().format == native_format(Format::Depth32) ||
                        destination->GetDesc().format == native_format(Format::Depth32) ||
                        source->GetDesc().format == native_format(Format::R32Float) ||
                        destination->GetDesc().format == native_format(Format::R32Float) ||
                        source->GetDesc().format != destination->GetDesc().format ||
                        source->GetDesc().sample_count != 1 || destination->GetDesc().sample_count != 1) {
                        last_status_ = INVALID_ARGUMENT;
                        return;
                    }
                    if (!adjust_saturation(*source, *destination, pass.saturation, command_list)) {
                        last_status_ = BACKEND_FAILED;
                        return;
                    }
                } else if (pass.operation == Operation::ScaleColor) {
                    if (source->GetDesc().format == native_format(Format::Depth32) ||
                        destination->GetDesc().format == native_format(Format::Depth32) ||
                        source->GetDesc().format == native_format(Format::R32Float) ||
                        destination->GetDesc().format == native_format(Format::R32Float) ||
                        source->GetDesc().format != destination->GetDesc().format ||
                        source->GetDesc().sample_count != 1 || destination->GetDesc().sample_count != 1) {
                        last_status_ = INVALID_ARGUMENT;
                        return;
                    }
                    if (!scale_color(*source, *destination, pass.color_scale, command_list)) {
                        last_status_ = BACKEND_FAILED;
                        return;
                    }
                } else if (pass.operation == Operation::TintColor) {
                    if (source->GetDesc().format == native_format(Format::Depth32) ||
                        destination->GetDesc().format == native_format(Format::Depth32) ||
                        source->GetDesc().format == native_format(Format::R32Float) ||
                        destination->GetDesc().format == native_format(Format::R32Float) ||
                        source->GetDesc().format != destination->GetDesc().format ||
                        source->GetDesc().sample_count != 1 || destination->GetDesc().sample_count != 1) {
                        last_status_ = INVALID_ARGUMENT;
                        return;
                    }
                    if (!tint_color(*source, *destination, pass.tint_red, pass.tint_green,
                        pass.tint_blue, pass.tint_alpha, command_list)) {
                        last_status_ = BACKEND_FAILED;
                        return;
                    }
                } else if (pass.operation == Operation::Fxaa) {
                    if ((source->GetDesc().format != native_format(Format::Rgba8) &&
                            source->GetDesc().format != native_format(Format::Rgba16Float) &&
                            source->GetDesc().format != native_format(Format::R11G11B10Float)) ||
                        (destination->GetDesc().format != native_format(Format::Rgba8) &&
                            destination->GetDesc().format != native_format(Format::Rgba16Float)) ||
                        source->GetDesc().sample_count != 1 || destination->GetDesc().sample_count != 1 ||
                        (static_cast<uint8_t>(destination->GetDesc().bind_flags) &
                            static_cast<uint8_t>(wi::graphics::BindFlag::UNORDERED_ACCESS)) == 0) {
                        last_status_ = INVALID_ARGUMENT;
                        return;
                    }
                    wi::renderer::Postprocess_FXAA(*source, *destination, command_list);
                } else if (pass.operation == Operation::Sharpen) {
                    if ((source->GetDesc().format != native_format(Format::Rgba8) &&
                            source->GetDesc().format != native_format(Format::Rgba16Float) &&
                            source->GetDesc().format != native_format(Format::R11G11B10Float)) ||
                        (destination->GetDesc().format != native_format(Format::Rgba8) &&
                            destination->GetDesc().format != native_format(Format::Rgba16Float)) ||
                        source->GetDesc().sample_count != 1 || destination->GetDesc().sample_count != 1 ||
                        (static_cast<uint8_t>(destination->GetDesc().bind_flags) &
                            static_cast<uint8_t>(wi::graphics::BindFlag::UNORDERED_ACCESS)) == 0) {
                        last_status_ = INVALID_ARGUMENT;
                        return;
                    }
                    wi::renderer::Postprocess_Sharpen(*source, *destination, command_list,
                        pass.sharpen_amount);
                } else if (pass.operation == Operation::ChromaticAberration) {
                    if ((source->GetDesc().format != native_format(Format::Rgba8) &&
                            source->GetDesc().format != native_format(Format::Rgba16Float) &&
                            source->GetDesc().format != native_format(Format::R11G11B10Float)) ||
                        (destination->GetDesc().format != native_format(Format::Rgba8) &&
                            destination->GetDesc().format != native_format(Format::Rgba16Float)) ||
                        source->GetDesc().sample_count != 1 || destination->GetDesc().sample_count != 1 ||
                        (static_cast<uint8_t>(destination->GetDesc().bind_flags) &
                            static_cast<uint8_t>(wi::graphics::BindFlag::UNORDERED_ACCESS)) == 0) {
                        last_status_ = INVALID_ARGUMENT;
                        return;
                    }
                    wi::renderer::Postprocess_Chromatic_Aberration(*source, *destination,
                        command_list, pass.chromatic_aberration);
                } else if (pass.operation == Operation::NormalsFromDepth) {
                    if (source->GetDesc().format != native_format(Format::R32Float) ||
                        source->GetDesc().sample_count != 1 ||
                        (static_cast<uint8_t>(source->GetDesc().bind_flags) &
                            static_cast<uint8_t>(wi::graphics::BindFlag::SHADER_RESOURCE)) == 0 ||
                        destination->GetDesc().format != native_format(Format::Rgba16Float) ||
                        destination->GetDesc().sample_count != 1 ||
                        (static_cast<uint8_t>(destination->GetDesc().bind_flags) &
                            static_cast<uint8_t>(wi::graphics::BindFlag::UNORDERED_ACCESS)) == 0) {
                        last_status_ = INVALID_ARGUMENT;
                        return;
                    }
                    wi::renderer::Postprocess_NormalsFromDepth(*source, *destination, command_list);
#if defined(ELISA_RENDER_SCENE_TEST_PROBE)
                    ++scene_depth_read_count_;
#endif
                } else if (source->GetDesc().format == native_format(Format::Depth32) ||
                    destination->GetDesc().format == native_format(Format::Depth32) ||
                    source->GetDesc().format != destination->GetDesc().format) {
                    last_status_ = INVALID_ARGUMENT;
                    return;
                } else if (pass.operation == Operation::CopyColor) {
                    if (source->GetDesc().sample_count != 1 || destination->GetDesc().sample_count != 1) {
                        last_status_ = INVALID_ARGUMENT;
                        return;
                    }
                    if (!copy_color(*source, *destination, command_list)) {
                        last_status_ = BACKEND_FAILED;
                        return;
                    }
                } else {
                    if (source->GetDesc().sample_count <= 1 || destination->GetDesc().sample_count != 1) {
                        last_status_ = INVALID_ARGUMENT;
                        return;
                    }
                    if (!resolve_color(*source, *destination, command_list)) {
                        last_status_ = BACKEND_FAILED;
                        return;
                    }
#if defined(ELISA_RENDER_SCENE_TEST_PROBE)
                    ++color_resolve_count_;
#endif
                }
            }
        }
        const uint32_t output_index = resource_index(active_, active_.output_id);
        wi::graphics::Texture* output = texture_for(active_.output_id, path);
        if (output == nullptr || output_index == NO_TARGET) {
            last_status_ = BACKEND_FAILED;
            return;
        }
        path.lastPostprocessRT = output;
        ++executions_;
        last_status_ = OK;
    }
