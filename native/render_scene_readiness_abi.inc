extern "C" int32_t elisa_render_scene_v1_wait_for_pipelines(uint32_t timeout_milliseconds) {
    constexpr uint32_t MAX_TIMEOUT_MILLISECONDS = 120000;
    constexpr uint32_t IDLE_CONFIRMATIONS = 3;
    constexpr auto POLL_INTERVAL = std::chrono::milliseconds(10);
    if (timeout_milliseconds == 0 || timeout_milliseconds > MAX_TIMEOUT_MILLISECONDS) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }

    RenderSceneService& state = service();
    {
        std::lock_guard<std::mutex> guard(state.mutex);
        if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
        if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    }

    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(timeout_milliseconds);
    uint32_t idle_confirmations = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        if (wi::renderer::IsPipelineCreationActive() == 0) {
            if (++idle_confirmations >= IDLE_CONFIRMATIONS) return ELISA_RENDER_SCENE_OK;
        } else {
            idle_confirmations = 0;
        }
        std::this_thread::sleep_for(POLL_INTERVAL);
    }
    return ELISA_RENDER_SCENE_TIMED_OUT;
}
