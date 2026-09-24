bool snapshot_placement_visible(const RenderSceneService& state, wi::ecs::Entity entity) {
    const wi::scene::ObjectComponent* object = state.scene->objects.GetComponent(entity);
    const wi::scene::TransformComponent* transform = state.scene->transforms.GetComponent(entity);
    if (object == nullptr || transform == nullptr) return true;
    const wi::scene::MeshComponent* mesh = state.scene->meshes.GetComponent(object->meshID);
    if (mesh == nullptr || !mesh->aabb.IsValid()) return true;
    return state.camera->frustum.CheckBoxFast(mesh->aabb.transform(transform->world));
}

bool snapshot_instance_visible(const RenderSceneService& state, const InstanceSlot& instance) {
    if (snapshot_placement_visible(state, instance.entity)) return true;
    for (wi::ecs::Entity entity : instance.snapshot_placement_entities) {
        if (snapshot_placement_visible(state, entity)) return true;
    }
    return false;
}
