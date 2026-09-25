namespace {

constexpr float MIN_BODY_MATERIAL_COEFFICIENT = 0.0f;
constexpr float MAX_BODY_FRICTION = 10.0f;
constexpr float MAX_BODY_RESTITUTION = 1.0f;

bool valid_body_material(float friction, float restitution) {
    return std::isfinite(friction) && std::isfinite(restitution) &&
        friction >= MIN_BODY_MATERIAL_COEFFICIENT &&
        friction <= MAX_BODY_FRICTION &&
        restitution >= MIN_BODY_MATERIAL_COEFFICIENT &&
        restitution <= MAX_BODY_RESTITUTION;
}

} // namespace

extern "C" int32_t elisa_physics_v1_body_set_material(uint64_t world_generation,
    uint32_t slot, uint64_t body_generation, float friction, float restitution) {
    if (!valid_body_material(friction, restitution)) return ELISA_PHYSICS_INVALID_ARGUMENT;
    const int32_t status = require_world(world_generation);
    if (status != ELISA_PHYSICS_OK) return status;
    PhysicsService& state = physics_service();
    BodySlot* body = resolve_body(state, slot, body_generation);
    if (body == nullptr) return ELISA_PHYSICS_INVALID_HANDLE;
    wi::scene::RigidBodyPhysicsComponent* rigidbody =
        state.scene->rigidbodies.GetComponent(body->entity);
    if (rigidbody == nullptr) return ELISA_PHYSICS_INVALID_HANDLE;
    rigidbody->friction = friction;
    rigidbody->restitution = restitution;
    return ELISA_PHYSICS_OK;
}
