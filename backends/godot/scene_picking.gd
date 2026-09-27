extends RefCounted

# Godot host picking keeps gameplay identity as metadata on physics bodies.
# Camera coordinates use the active Viewport's framebuffer pixel space.
enum Status { INVALID_ARGUMENT, MISS, NOT_SELECTABLE, HIT }

const EPOCH_KEY: StringName = &"elisa_gameplay_epoch"
const ENTITY_KEY: StringName = &"elisa_gameplay_id"
const MAX_COLLISION_MASK: int = 0xffffffff
const MAX_PICK_DISTANCE := 10000.0

static func bind_identity(body: CollisionObject3D, gameplay_epoch: int, gameplay_id: int) -> bool:
	if body == null or gameplay_epoch <= 0 or gameplay_id <= 0:
		return false
	body.set_meta(EPOCH_KEY, gameplay_epoch)
	body.set_meta(ENTITY_KEY, gameplay_id)
	return true

static func pick(camera: Camera3D, viewport_point: Vector2, collision_mask: int) -> Dictionary:
	if camera == null or not is_instance_valid(camera) or not camera.is_inside_tree() \
			or collision_mask <= 0 or collision_mask > MAX_COLLISION_MASK \
			or not is_finite(viewport_point.x) or not is_finite(viewport_point.y):
		return _empty_result(Status.INVALID_ARGUMENT)
	var viewport := camera.get_viewport()
	var visible := viewport.get_visible_rect()
	if not visible.has_point(viewport_point):
		return _empty_result(Status.INVALID_ARGUMENT)
	var world := camera.get_world_3d()
	if world == null:
		return _empty_result(Status.INVALID_ARGUMENT)
	var origin := camera.project_ray_origin(viewport_point)
	var direction := camera.project_ray_normal(viewport_point)
	var direction_length_squared := direction.length_squared()
	if not _finite_vector3(origin) or not _finite_vector3(direction) \
			or not is_finite(direction_length_squared) or direction_length_squared <= 0.0:
		return _empty_result(Status.INVALID_ARGUMENT)
	var query := PhysicsRayQueryParameters3D.create(
		origin, origin + direction.normalized() * MAX_PICK_DISTANCE, collision_mask)
	query.collide_with_areas = true
	var hit := world.direct_space_state.intersect_ray(query)
	if hit.is_empty():
		return _empty_result(Status.MISS)
	var body := hit.get("collider") as CollisionObject3D
	if body == null or not body.has_meta(EPOCH_KEY) or not body.has_meta(ENTITY_KEY):
		return _not_selectable(body)
	var epoch_value: Variant = body.get_meta(EPOCH_KEY)
	var entity_value: Variant = body.get_meta(ENTITY_KEY)
	if typeof(epoch_value) != TYPE_INT or typeof(entity_value) != TYPE_INT:
		return _not_selectable(body)
	var epoch: int = epoch_value
	var entity_id: int = entity_value
	if epoch <= 0 or entity_id <= 0:
		return _not_selectable(body)
	return {
		"status": Status.HIT,
		"gameplay_epoch": epoch,
		"gameplay_id": entity_id,
		"distance": origin.distance_to(hit["position"]),
		"collider": body,
	}

static func _empty_result(status: Status) -> Dictionary:
	return {
		"status": status,
		"gameplay_epoch": 0,
		"gameplay_id": 0,
		"distance": 0.0,
		"collider": null,
	}

static func _not_selectable(body: CollisionObject3D) -> Dictionary:
	var result := _empty_result(Status.NOT_SELECTABLE)
	result["collider"] = body
	return result

static func _finite_vector3(value: Vector3) -> bool:
	return is_finite(value.x) and is_finite(value.y) and is_finite(value.z)
