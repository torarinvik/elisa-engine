void clear_instance_pick_bindings(RenderSceneService& state, InstanceSlot& instance) {
    if (state.picking != nullptr) {
        for (probe::PickBinding binding : instance.pick_bindings) {
            state.picking->unbind(binding);
        }
    }
    instance.pick_bindings.clear();
}

bool set_instance_pick_identity(RenderSceneService& state, InstanceSlot& instance,
    int64_t gameplay_epoch, int64_t gameplay_id) {
    if (gameplay_epoch <= 0 || gameplay_id <= 0 || state.picking == nullptr) return false;
    const probe::PickGameplayRef gameplay{gameplay_epoch, gameplay_id};
    std::vector<probe::PickBinding> candidates;
    const auto bind = [&](wi::ecs::Entity entity) {
        if (entity == wi::ecs::INVALID_ENTITY) return true;
        const probe::PickBinding binding = state.picking->bind(entity, gameplay);
        if (binding.owner == 0) return false;
        candidates.push_back(binding);
        return true;
    };
    if (!bind(instance.entity)) {
        for (probe::PickBinding binding : candidates) state.picking->unbind(binding);
        return false;
    }
    for (wi::ecs::Entity entity : instance.imported_mesh_entities) {
        if (!bind(entity)) {
            for (probe::PickBinding binding : candidates) state.picking->unbind(binding);
            return false;
        }
    }
    clear_instance_pick_bindings(state, instance);
    instance.pick_bindings = std::move(candidates);
    instance.gameplay_epoch = gameplay_epoch;
    instance.gameplay_id = gameplay_id;
    return true;
}
