#include <cfloat>
// R06: world bounds of a physics body's collision shape, in Elisa coordinates.
// The shape's local box (scale already baked in, as Jolt builds it) sits at the
// body's local_offset; its eight corners are rotated and translated by the body
// transform in Wicked space, then mapped back through from_wicked.
namespace {

bool physics_shape_half_extents(const wi::scene::RigidBodyPhysicsComponent& body,
    const XMFLOAT3& scale, XMFLOAT3& half) {
    using Shape = wi::scene::RigidBodyPhysicsComponent::CollisionShape;
    const XMFLOAT3 magnitude(std::fabs(scale.x), std::fabs(scale.y), std::fabs(scale.z));
    switch (body.shape) {
    case Shape::BOX:
        half = XMFLOAT3(body.box.halfextents.x * magnitude.x,
            body.box.halfextents.y * magnitude.y, body.box.halfextents.z * magnitude.z);
        return true;
    case Shape::SPHERE: {
        const float radius = body.sphere.radius * magnitude.x;
        half = XMFLOAT3(radius, radius, radius);
        return true;
    }
    case Shape::CAPSULE: {
        const float radius = body.capsule.radius * magnitude.x;
        half = XMFLOAT3(radius, body.capsule.height * magnitude.y + radius, radius);
        return true;
    }
    case Shape::CYLINDER: {
        const float radius = body.capsule.radius * magnitude.x;
        half = XMFLOAT3(radius, body.capsule.height * magnitude.y, radius);
        return true;
    }
    default:
        return false;
    }
}

} // namespace

extern "C" int32_t elisa_physics_v1_body_bounds(uint64_t world_generation,
    uint32_t slot, uint64_t body_generation, float* min_x, float* min_y, float* min_z,
    float* max_x, float* max_y, float* max_z) {
    if (min_x == nullptr || min_y == nullptr || min_z == nullptr ||
        max_x == nullptr || max_y == nullptr || max_z == nullptr) {
        return ELISA_PHYSICS_INVALID_ARGUMENT;
    }
    const int32_t status = require_world(world_generation);
    if (status != ELISA_PHYSICS_OK) return status;
    PhysicsService& state = physics_service();
    BodySlot* body = resolve_body(state, slot, body_generation);
    if (body == nullptr) return ELISA_PHYSICS_INVALID_HANDLE;
    const wi::scene::RigidBodyPhysicsComponent* rigidbody =
        state.scene->rigidbodies.GetComponent(body->entity);
    const wi::scene::TransformComponent* transform =
        state.scene->transforms.GetComponent(body->entity);
    if (rigidbody == nullptr || transform == nullptr) return ELISA_PHYSICS_INVALID_HANDLE;
    XMFLOAT3 half;
    if (!physics_shape_half_extents(*rigidbody, transform->GetScale(), half)) {
        return ELISA_PHYSICS_INVALID_ARGUMENT;
    }
    const XMVECTOR rotation = transform->GetRotationV();
    const XMVECTOR position = transform->GetPositionV();
    const XMVECTOR offset = XMLoadFloat3(&rigidbody->local_offset);
    XMFLOAT3 low(FLT_MAX, FLT_MAX, FLT_MAX);
    XMFLOAT3 high(-FLT_MAX, -FLT_MAX, -FLT_MAX);
    for (int corner = 0; corner < 8; ++corner) {
        const XMVECTOR local = XMVectorAdd(offset, XMVectorSet(
            (corner & 1) ? half.x : -half.x, (corner & 2) ? half.y : -half.y,
            (corner & 4) ? half.z : -half.z, 0.0f));
        XMFLOAT3 wicked;
        XMStoreFloat3(&wicked, XMVectorAdd(position, XMVector3Rotate(local, rotation)));
        const XMFLOAT3 elisa = probe::coordinates::from_wicked(wicked);
        low = XMFLOAT3(std::fmin(low.x, elisa.x), std::fmin(low.y, elisa.y), std::fmin(low.z, elisa.z));
        high = XMFLOAT3(std::fmax(high.x, elisa.x), std::fmax(high.y, elisa.y), std::fmax(high.z, elisa.z));
    }
    *min_x = low.x; *min_y = low.y; *min_z = low.z;
    *max_x = high.x; *max_y = high.y; *max_z = high.z;
    return ELISA_PHYSICS_OK;
}
