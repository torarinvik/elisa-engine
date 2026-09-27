extern "C" int32_t elisa_render_scene_v1_test_arc_capacity(void) {
    std::array<int64_t, MAX_ELECTRIC_ARCS> handles{};
    size_t created = 0;
    for (; created < MAX_ELECTRIC_ARCS; ++created) {
        const int64_t handle = elisa_render_scene_v1_create_electric_arc(0.02f, 0.0f, 7);
        if (handle <= 0) break;
        handles[created] = handle;
    }

    const int64_t overflow = created == MAX_ELECTRIC_ARCS
        ? elisa_render_scene_v1_create_electric_arc(0.02f, 0.0f, 7)
        : ELISA_RENDER_SCENE_BACKEND_FAILED;
    bool cleanup_ok = true;
    if (overflow > 0) cleanup_ok = elisa_render_scene_v1_destroy_electric_arc(overflow) == ELISA_RENDER_SCENE_OK;
    for (size_t index = 0; index < created; ++index) {
        cleanup_ok = (elisa_render_scene_v1_destroy_electric_arc(handles[index]) == ELISA_RENDER_SCENE_OK) && cleanup_ok;
    }
    return created == MAX_ELECTRIC_ARCS && overflow == ELISA_RENDER_SCENE_CAPACITY && cleanup_ok ? 1 : 0;
}
