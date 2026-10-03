// Setup may allocate; per-frame reductions use bounded stack buffers only.
extern "C" int32_t elisa_render_scene_v1_set_skin_contact_region(int64_t handle,
    uint32_t region, uint32_t placement, const uint32_t* vertices, uint32_t count) {
    if (!vertices || region >= 8 || count == 0 || count > 4096)
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    auto& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    size_t slot = MAX_INSTANCES;
    if (!valid_handle(state, handle, slot)) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    for (uint32_t first = 0; first < count; first += 64) {
        ElisaRenderSceneSkinReadback sample{};
        const int32_t status = read_skin_vertices_locked(state, slot, placement,
            vertices + first, std::min(64u, count-first), &sample);
        if (status != ELISA_RENDER_SCENE_OK) return status;
    }
    // Replacement is transactional, including allocation failure.
    try {
        std::vector<uint32_t> staged(vertices, vertices+count);
        std::sort(staged.begin(), staged.end());
        if (std::adjacent_find(staged.begin(), staged.end()) != staged.end())
            return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
        auto& target = state.instances[slot].skin_contact_regions[region];
        target.vertices.swap(staged);
        target.placement = placement;
    } catch (...) { return ELISA_RENDER_SCENE_BACKEND_FAILED; }
    return ELISA_RENDER_SCENE_OK;
}

// Setup from skeleton joint indices, independent of mesh vertex ordering.
extern "C" int32_t elisa_render_scene_v1_set_skin_contact_joints(int64_t handle,
    uint32_t region, uint32_t placement, const uint32_t* joints, uint32_t count, float threshold) {
#if defined(ELISA_RENDER_SCENE_TEST_PROBE)
    std::fprintf(stderr, "CONTACT_SETUP input handle=%lld region=%u placement=%u count=%u threshold=%f\n", (long long)handle, region, placement, count, threshold);
#endif
    if (!joints || region >= 8 || count == 0 || count > 64 ||
        !std::isfinite(threshold) || threshold < 0 || threshold >= 1)
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    auto& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    size_t slot = MAX_INSTANCES;
    if (!valid_handle(state, handle, slot)) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    auto& instance = state.instances[slot];
    if (placement > instance.imported_mesh_entities.size()) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    const auto entity = placement == 0 ? instance.entity : instance.imported_mesh_entities[placement-1];
    const auto* object = state.scene->objects.GetComponent(entity);
    const auto* mesh = object ? state.scene->meshes.GetComponent(object->meshID) : nullptr;
    const auto* armature = mesh ? state.scene->armatures.GetComponent(mesh->armatureID) : nullptr;
    if (!mesh || !armature || mesh->vertex_boneindices.size() != mesh->vertex_positions.size() ||
        mesh->vertex_boneweights.size() != mesh->vertex_positions.size()) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    if (armature->boneCollection.size() > 256) return ELISA_RENDER_SCENE_CAPACITY;
#if defined(ELISA_RENDER_SCENE_TEST_PROBE)
    std::fprintf(stderr, "CONTACT_SETUP region=%u joints=%u,%u threshold=%f bones=%zu vertices=%zu\n", region,
        joints[0], count > 1 ? joints[1] : joints[0], threshold, armature->boneCollection.size(), mesh->vertex_positions.size());
#endif
    bool selected[256]{};
    for (uint32_t index = 0; index < count; ++index) {
        if (joints[index] >= instance.joint_entities.size()) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
        for (uint32_t prior = 0; prior < index; ++prior)
            if (joints[prior] == joints[index]) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
        const auto joint = instance.joint_entities[joints[index]];
        bool found = false;
        for (size_t cluster = 0; cluster < armature->boneCollection.size(); ++cluster)
            if (armature->boneCollection[cluster] == joint) { selected[cluster] = true; found = true; }
        if (!found) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    const bool second = !mesh->vertex_boneindices2.empty();
    if (second && (mesh->vertex_boneindices2.size() != mesh->vertex_positions.size() ||
        mesh->vertex_boneweights2.size() != mesh->vertex_positions.size())) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    try {
        std::vector<uint32_t> staged;
        staged.reserve(4096);
        for (size_t vertex = 0; vertex < mesh->vertex_positions.size(); ++vertex) {
            float combined = 0, total = 0;
            for (uint32_t group = 0; group < (second ? 2u : 1u); ++group) {
                const auto& indices = group == 0 ? mesh->vertex_boneindices[vertex] : mesh->vertex_boneindices2[vertex];
                const auto& weights = group == 0 ? mesh->vertex_boneweights[vertex] : mesh->vertex_boneweights2[vertex];
                const uint32_t clusters[] = {indices.x, indices.y, indices.z, indices.w};
                const float values[] = {weights.x, weights.y, weights.z, weights.w};
                for (uint32_t influence = 0; influence < 4; ++influence) {
                    const float weight = values[influence];
                    if (!std::isfinite(weight) || weight < 0) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
                    if (weight == 0) continue;
                    if (clusters[influence] >= armature->boneCollection.size()) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
                    total += weight;
                    if (selected[clusters[influence]]) combined += weight;
                }
            }
            if (std::abs(total-1.f) > .002f) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
            if (combined > threshold) {
                if (staged.size() == 4096) return ELISA_RENDER_SCENE_CAPACITY;
                staged.push_back(uint32_t(vertex));
            }
        }
#if defined(ELISA_RENDER_SCENE_TEST_PROBE)
        std::fprintf(stderr, "CONTACT_SETUP selected=%zu\n", staged.size());
#endif
        if (staged.empty()) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
        SkinMatrixPalette palette;
        for (size_t first = 0; first < staged.size(); first += 64) {
            ElisaRenderSceneSkinReadback sample{};
            const int32_t status = read_skin_vertices_locked(state, slot, placement,
                staged.data()+first, uint32_t(std::min(size_t(64), staged.size()-first)), &sample, &palette);
            if (status != ELISA_RENDER_SCENE_OK) return status;
        }
        auto& target = instance.skin_contact_regions[region];
        target.vertices.swap(staged);
        target.placement = placement;
    } catch (...) { return ELISA_RENDER_SCENE_BACKEND_FAILED; }
    return ELISA_RENDER_SCENE_OK;
}

extern "C" int32_t elisa_render_scene_v1_read_skin_contact(int64_t handle,
    uint32_t region, float nx, float ny, float nz, float offset,
    ElisaRenderSceneSkinContact* output) {
    const float norm = nx*nx + ny*ny + nz*nz;
    if (!output || region >= 8 || !std::isfinite(norm) ||
        std::abs(norm-1.f) > .002f || !std::isfinite(offset))
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    auto& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    size_t slot = MAX_INSTANCES;
    if (!valid_handle(state, handle, slot)) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    const auto& target = state.instances[slot].skin_contact_regions[region];
    if (target.vertices.empty()) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    ElisaRenderSceneSkinContact result{};
    result.clearance = std::numeric_limits<float>::infinity();
    double sum[3]{};
    SkinMatrixPalette palette;
    for (size_t first = 0; first < target.vertices.size(); first += 64) {
        ElisaRenderSceneSkinReadback sample{};
        const uint32_t count = uint32_t(std::min(size_t(64), target.vertices.size()-first));
        const int32_t status = read_skin_vertices_locked(state, slot, target.placement,
            target.vertices.data()+first, count, &sample, &palette);
        if (status != ELISA_RENDER_SCENE_OK) return status;
        for (uint32_t index = 0; index < count; ++index) {
            const float* p = sample.points + index*6;
            const float clearance = nx*p[3] + ny*p[4] + nz*p[5] + offset;
            if (!std::isfinite(clearance)) return ELISA_RENDER_SCENE_BACKEND_FAILED;
            for (uint32_t axis = 0; axis < 3; ++axis) sum[axis] += p[axis+3];
            if (clearance < result.clearance) {
                result.clearance = clearance;
                for (uint32_t axis = 0; axis < 3; ++axis) {
                    result.model_point[axis] = p[axis];
                    result.world_point[axis] = p[axis+3];
                }
            }
        }
    }
    result.vertices = uint32_t(target.vertices.size());
    for (uint32_t axis = 0; axis < 3; ++axis)
        result.world_center[axis] = float(sum[axis]/result.vertices);
    *output = result;
    return ELISA_RENDER_SCENE_OK;
}

#if defined(ELISA_RENDER_SCENE_TEST_PROBE)
extern "C" uint32_t elisa_render_scene_v1_test_skin_vertex_count(int64_t handle) {
    auto& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    size_t slot = MAX_INSTANCES;
    if (!state.initialized || !on_owner_thread(state) || !valid_handle(state, handle, slot)) return 0;
    const auto* object = state.scene->objects.GetComponent(state.instances[slot].entity);
    const auto* mesh = object ? state.scene->meshes.GetComponent(object->meshID) : nullptr;
    return mesh ? uint32_t(mesh->vertex_positions.size()) : 0;
}
#endif

#if defined(ELISA_RENDER_SCENE_TEST_PROBE)
extern "C" int32_t elisa_render_scene_v1_test_skin_contact_cost(
    int64_t handle, uint32_t region, uint32_t iterations) {
    if (iterations == 0 || iterations > 1000) return 0;
    ElisaRenderSceneSkinContact result{};
    if (elisa_render_scene_v1_read_skin_contact(handle, region, 0, 1, 0, 0, &result) != ELISA_RENDER_SCENE_OK) return 0;
    const auto start = std::chrono::steady_clock::now();
    for (uint32_t index = 0; index < iterations; ++index)
        if (elisa_render_scene_v1_read_skin_contact(handle, region, 0, 1, 0, 0, &result) != ELISA_RENDER_SCENE_OK) return 0;
    const double microseconds = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now()-start).count();
    std::printf("SKIN_CONTACT_COST region=%u vertices=%u iterations=%u mean_us=%.3f\n",
        region, result.vertices, iterations, microseconds/iterations);
    return 1;
}
#endif
