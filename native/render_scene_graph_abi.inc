extern "C" int32_t elisa_render_scene_v1_render_graph_begin(
    uint32_t resource_count, uint32_t pass_count, uint32_t output_id) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    return state.render_graph.begin(resource_count, pass_count, output_id);
}

extern "C" int32_t elisa_render_scene_v1_render_graph_set_resource(
    uint32_t index, uint32_t id, int32_t size_mode, uint32_t width, uint32_t height,
    int32_t format, uint32_t samples, int32_t lifetime, int32_t initialized,
    uint32_t target_slot, int32_t import_source) {
    if (initialized != 0 && initialized != 1) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    const elisa::render_graph::Resource resource{
        id,
        static_cast<elisa::render_graph::SizeMode>(size_mode),
        width,
        height,
        static_cast<elisa::render_graph::Format>(format),
        samples,
        static_cast<elisa::render_graph::Lifetime>(lifetime),
        initialized != 0,
        target_slot,
        static_cast<elisa::render_graph::ImportSource>(import_source),
    };
    return state.render_graph.set_resource(index, resource);
}

extern "C" int32_t elisa_render_scene_v1_render_graph_set_pass(
    uint32_t index, uint32_t id, int32_t operation, uint32_t source_id,
    uint32_t destination_id) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    const elisa::render_graph::Pass pass{
        id,
        static_cast<elisa::render_graph::Operation>(operation),
        source_id,
        destination_id,
    };
    return state.render_graph.set_pass(index, pass);
}

extern "C" int32_t elisa_render_scene_v1_render_graph_set_pass_with_opacity(
    uint32_t index, uint32_t id, int32_t operation, uint32_t source_id,
    uint32_t destination_id, float opacity) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    const elisa::render_graph::Pass pass{
        id,
        static_cast<elisa::render_graph::Operation>(operation),
        source_id,
        destination_id,
        opacity,
    };
    return state.render_graph.set_pass(index, pass);
}

extern "C" int32_t elisa_render_scene_v1_render_graph_set_pass_with_saturation(
    uint32_t index, uint32_t id, int32_t operation, uint32_t source_id,
    uint32_t destination_id, float saturation) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    const elisa::render_graph::Pass pass{
        id,
        static_cast<elisa::render_graph::Operation>(operation),
        source_id,
        destination_id,
        elisa::render_graph::DEFAULT_BLEND_OPACITY,
        saturation,
    };
    return state.render_graph.set_pass(index, pass);
}

extern "C" int32_t elisa_render_scene_v1_render_graph_set_pass_with_color_scale(
    uint32_t index, uint32_t id, int32_t operation, uint32_t source_id,
    uint32_t destination_id, float color_scale) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    const elisa::render_graph::Pass pass{
        id,
        static_cast<elisa::render_graph::Operation>(operation),
        source_id,
        destination_id,
        elisa::render_graph::DEFAULT_BLEND_OPACITY,
        elisa::render_graph::DEFAULT_SATURATION,
        color_scale,
    };
    return state.render_graph.set_pass(index, pass);
}

extern "C" int32_t elisa_render_scene_v1_render_graph_set_pass_with_tint(
    uint32_t index, uint32_t id, int32_t operation, uint32_t source_id,
    uint32_t destination_id, float red, float green, float blue, float alpha) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    elisa::render_graph::Pass pass{
        id,
        static_cast<elisa::render_graph::Operation>(operation),
        source_id,
        destination_id,
    };
    pass.tint_red = red;
    pass.tint_green = green;
    pass.tint_blue = blue;
    pass.tint_alpha = alpha;
    return state.render_graph.set_pass(index, pass);
}

extern "C" int32_t elisa_render_scene_v1_render_graph_set_pass_with_sharpen(
    uint32_t index, uint32_t id, int32_t operation, uint32_t source_id,
    uint32_t destination_id, float amount) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    elisa::render_graph::Pass pass{
        id,
        static_cast<elisa::render_graph::Operation>(operation),
        source_id,
        destination_id,
    };
    pass.sharpen_amount = amount;
    return state.render_graph.set_pass(index, pass);
}

extern "C" int32_t elisa_render_scene_v1_render_graph_set_pass_with_chromatic_aberration(
    uint32_t index, uint32_t id, int32_t operation, uint32_t source_id,
    uint32_t destination_id, float amount) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    elisa::render_graph::Pass pass{
        id,
        static_cast<elisa::render_graph::Operation>(operation),
        source_id,
        destination_id,
    };
    pass.chromatic_aberration = amount;
    return state.render_graph.set_pass(index, pass);
}

extern "C" int32_t elisa_render_scene_v1_render_graph_commit(void) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    return state.render_graph.commit();
}

extern "C" int32_t elisa_render_scene_v1_render_graph_abort(void) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    state.render_graph.abort();
    return ELISA_RENDER_SCENE_OK;
}

extern "C" int32_t elisa_render_scene_v1_render_graph_status(void) {
    return service().render_graph.last_status();
}

extern "C" uint64_t elisa_render_scene_v1_render_graph_execution_count(void) {
    return service().render_graph.execution_count();
}

#if defined(ELISA_RENDER_SCENE_TEST_PROBE)
extern "C" int32_t elisa_render_scene_v1_render_graph_test_fail_next_allocation(void) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    return state.render_graph.fail_next_allocation_for_test();
}

extern "C" int32_t elisa_render_scene_v1_render_graph_test_fail_pass(uint32_t index) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    return state.render_graph.fail_pass_for_test(index);
}

extern "C" int32_t elisa_render_scene_v1_render_graph_test_suspend_next_frame(void) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    return state.render_graph.suspend_next_frame_for_test();
}

extern "C" int32_t elisa_render_scene_v1_render_graph_test_fallback_preserved(void) {
    return service().render_graph.last_fallback_preserved_for_test() ? 1 : 0;
}

extern "C" uint64_t elisa_render_scene_v1_render_graph_test_depth_clear_count(void) {
    return service().render_graph.depth_clear_count_for_test();
}

extern "C" uint64_t elisa_render_scene_v1_render_graph_test_color_resolve_count(void) {
    return service().render_graph.color_resolve_count_for_test();
}

extern "C" uint64_t elisa_render_scene_v1_render_graph_test_scene_depth_read_count(void) {
    return service().render_graph.scene_depth_read_count_for_test();
}

extern "C" uint64_t elisa_render_scene_v1_render_graph_test_device_memory_usage(void) {
    wi::graphics::GraphicsDevice* device = wi::graphics::GetDevice();
    return device == nullptr ? 0 : device->GetMemoryUsage().usage;
}

extern "C" uint32_t elisa_render_scene_v1_render_graph_test_retirement_frame_count(void) {
    return wi::graphics::GraphicsDevice::GetBufferCount() + 1;
}

extern "C" int32_t elisa_render_scene_v1_render_graph_test_wait_for_gpu(void) {
    wi::graphics::GraphicsDevice* device = wi::graphics::GetDevice();
    if (device == nullptr) return 0;
    device->WaitForGPU();
    return 1;
}
#endif
