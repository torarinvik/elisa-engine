// Bounded, allocation-free CPU skin samples from current scene transforms.
// Do not use armature.boneData: the render update can lag a pose override.
static int32_t read_skin_vertices_locked(RenderSceneService& state, size_t slot,
    uint32_t placement, const uint32_t* vertices, uint32_t count,
    ElisaRenderSceneSkinReadback* output) {
    if (!vertices || !output || count == 0 || count > 64)
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    const auto& instance = state.instances[slot];
    if (placement > instance.imported_mesh_entities.size()) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    const auto entity = placement == 0 ? instance.entity : instance.imported_mesh_entities[placement-1];
    const auto* object = state.scene->objects.GetComponent(entity);
    const auto* mesh = object ? state.scene->meshes.GetComponent(object->meshID) : nullptr;
    const auto* world = state.scene->transforms.GetComponent(entity);
    const auto* root = state.scene->transforms.GetComponent(instance.entity);
    const auto* armature = mesh ? state.scene->armatures.GetComponent(mesh->armatureID) : nullptr;
    const auto* armature_world = mesh ? state.scene->transforms.GetComponent(mesh->armatureID) : nullptr;
    if (!mesh || !world || !root || !armature || !armature_world ||
        armature->boneCollection.size() != armature->inverseBindMatrices.size() ||
        mesh->vertex_boneindices.size() != mesh->vertex_positions.size() ||
        mesh->vertex_boneweights.size() != mesh->vertex_positions.size())
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    // Active morph support must precede accepting morph-deformed contact data.
    for (const auto& morph : mesh->morph_targets)
        if (!std::isfinite(morph.weight) || morph.weight != 0)
            return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    const bool second = !mesh->vertex_boneindices2.empty();
    if (second && (mesh->vertex_boneindices2.size() != mesh->vertex_positions.size() ||
        mesh->vertex_boneweights2.size() != mesh->vertex_positions.size()))
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    const auto invert = [](const XMFLOAT4X4& matrix, XMMATRIX& inverse) {
        XMVECTOR determinant;
        inverse = XMMatrixInverse(&determinant, XMLoadFloat4x4(&matrix));
        const float value = XMVectorGetX(determinant);
        return std::isfinite(value) && std::abs(value) > 1e-12f;
    };
    XMMATRIX root_inverse, armature_inverse;
    if (!invert(root->world, root_inverse) || !invert(armature_world->world, armature_inverse))
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    ElisaRenderSceneSkinReadback result{};
    for (uint32_t sample = 0; sample < count; ++sample) {
        const uint32_t vertex = vertices[sample];
        if (vertex >= mesh->vertex_positions.size()) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
        XMVECTOR skinned = XMVectorZero();
        float total = 0;
        for (uint32_t group = 0; group < (second ? 2u : 1u); ++group) {
            const auto& indices = group == 0 ? mesh->vertex_boneindices[vertex] : mesh->vertex_boneindices2[vertex];
            const auto& weights = group == 0 ? mesh->vertex_boneweights[vertex] : mesh->vertex_boneweights2[vertex];
            const uint32_t joints[] = {indices.x, indices.y, indices.z, indices.w};
            const float values[] = {weights.x, weights.y, weights.z, weights.w};
            for (uint32_t influence = 0; influence < 4; ++influence) {
                const float weight = values[influence];
                if (!std::isfinite(weight) || weight < 0) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
                if (weight == 0) continue;
                const auto cluster = joints[influence];
                if (cluster >= armature->boneCollection.size()) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
                const auto* bone = state.scene->transforms.GetComponent(armature->boneCollection[cluster]);
                if (!bone) return ELISA_RENDER_SCENE_BACKEND_FAILED;
                const XMMATRIX skin = XMLoadFloat4x4(&armature->inverseBindMatrices[cluster]) *
                    XMLoadFloat4x4(&bone->world) * armature_inverse;
                skinned += XMVector3Transform(XMLoadFloat3(&mesh->vertex_positions[vertex]), skin) * weight;
                total += weight;
            }
        }
        if (std::abs(total - 1.f) > .002f) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
        const XMVECTOR point = XMVector3Transform(skinned, XMLoadFloat4x4(&world->world));
        XMFLOAT3 world_point, model_point;
        XMStoreFloat3(&world_point, point);
        XMStoreFloat3(&model_point, XMVector3Transform(point, root_inverse));
        world_point = probe::coordinates::from_wicked(world_point);
        model_point = probe::coordinates::from_wicked(model_point);
        const float values[] = {model_point.x, model_point.y, model_point.z,
            world_point.x, world_point.y, world_point.z};
        for (uint32_t component = 0; component < 6; ++component) {
            if (!std::isfinite(values[component])) return ELISA_RENDER_SCENE_BACKEND_FAILED;
            result.points[sample * 6 + component] = values[component];
        }
    }
    result.count = count;
    *output = result; // Every failure leaves caller output unchanged.
    return ELISA_RENDER_SCENE_OK;
}

extern "C" int32_t elisa_render_scene_v1_read_skinned_vertices(
    int64_t handle, uint32_t placement, const uint32_t* vertices, uint32_t count,
    ElisaRenderSceneSkinReadback* output) {
    if (!vertices || !output || count == 0 || count > 64)
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    auto& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    size_t slot = MAX_INSTANCES;
    if (!valid_handle(state, handle, slot)) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    return read_skin_vertices_locked(state, slot, placement, vertices, count, output);
}

#if defined(ELISA_RENDER_SCENE_TEST_PROBE)
extern "C" int32_t elisa_render_scene_v1_test_skin_readback_atomic(int64_t handle) {
    ElisaRenderSceneSkinReadback sentinel;
    std::memset(&sentinel, 0xa5, sizeof(sentinel));
    const uint32_t vertices[] = {0, UINT32_MAX};
    const auto fails_unchanged = [&](uint32_t placement, uint32_t count) {
        auto output = sentinel;
        const int32_t status = elisa_render_scene_v1_read_skinned_vertices(
            handle, placement, vertices, count, &output);
        return status != ELISA_RENDER_SCENE_OK &&
            std::memcmp(&output, &sentinel, sizeof(output)) == 0;
    };
    return fails_unchanged(0, 2) && fails_unchanged(UINT32_MAX, 1) &&
        fails_unchanged(0, 0) && fails_unchanged(0, 65) ? 1 : 0;
}
#endif
