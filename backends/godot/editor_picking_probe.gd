extends RefCounted

const ScenePicking = preload("res://scene_picking.gd")
const ElisaSelection = preload("res://editor_selection.gd")
const OutlineShader = preload("res://selection_outline.gdshader")

const COLLISION_MASK := 1
const CAMERA_DISTANCE := 8.0
const BLOCKER_DISTANCE := 4.0
const BOX_SIZE := Vector3(2.0, 2.0, 2.0)
const TEST_DISTANCE := 7.0
const DISTANCE_TOLERANCE := 0.05
const INVALID_COLLISION_MASK := 4294967296
const OUTLINE_WIDTH := 0.08

static func run(parent: Node3D, gameplay_epoch: int, gameplay_id: int) -> String:
	var scene := Node3D.new()
	scene.name = "ElisaPickingProbe"
	parent.add_child(scene)
	var camera := Camera3D.new()
	camera.position = Vector3(0.0, 0.0, CAMERA_DISTANCE)
	scene.add_child(camera)
	camera.look_at(Vector3.ZERO, Vector3.UP)
	camera.current = true

	var target := _box_body(scene, "SelectedEntity", Vector3.ZERO, COLLISION_MASK)
	var visual := MeshInstance3D.new()
	var mesh := BoxMesh.new()
	mesh.size = BOX_SIZE
	var base_material := StandardMaterial3D.new()
	base_material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mesh.material = base_material
	visual.mesh = mesh
	var previous_overlay := StandardMaterial3D.new()
	visual.material_overlay = previous_overlay
	target.add_child(visual)
	if not ScenePicking.bind_identity(target, gameplay_epoch, gameplay_id, visual):
		return "valid gameplay identity was rejected"
	if ScenePicking.bind_identity(target, 0, gameplay_id):
		return "zero gameplay epoch was accepted"
	await parent.get_tree().physics_frame
	await parent.get_tree().physics_frame

	var center := camera.get_viewport().get_visible_rect().get_center()
	var hit := ScenePicking.pick(camera, center, COLLISION_MASK)
	if hit.status != ScenePicking.Status.HIT or hit.gameplay_epoch != gameplay_epoch \
			or hit.gameplay_id != gameplay_id or hit.collider != target \
			or hit.visual != visual \
			or absf(hit.distance - TEST_DISTANCE) > DISTANCE_TOLERANCE:
		return "Godot camera ray did not return the bound gameplay identity"
	var masked := ScenePicking.pick(camera, center, 2)
	if masked.status != ScenePicking.Status.MISS:
		return "collision mask did not exclude the entity"
	var miss := ScenePicking.pick(camera, Vector2.ZERO, COLLISION_MASK)
	if miss.status != ScenePicking.Status.MISS:
		return "off-target viewport ray should miss"
	var invalid := ScenePicking.pick(camera, Vector2(-1.0, 0.0), COLLISION_MASK)
	if invalid.status != ScenePicking.Status.INVALID_ARGUMENT:
		return "out-of-viewport ray was accepted"
	if ScenePicking.pick(null, center, COLLISION_MASK).status != ScenePicking.Status.INVALID_ARGUMENT:
		return "null camera was accepted"
	if ScenePicking.pick(camera, center, INVALID_COLLISION_MASK).status != ScenePicking.Status.INVALID_ARGUMENT:
		return "collision mask wider than 32 bits was accepted"
	var outline := ShaderMaterial.new()
	outline.shader = OutlineShader
	outline.set_shader_parameter("outline_width", OUTLINE_WIDTH)
	var selection = ElisaSelection.new()
	var malformed_pick: Dictionary = hit.duplicate()
	malformed_pick["gameplay_epoch"] = str(gameplay_epoch)
	if selection.apply_pick(malformed_pick, outline):
		return "selection accepted a non-integer epoch"
	if not selection.apply_pick(hit, outline) or visual.material_overlay != outline:
		return "Godot outline material was not applied to the selected visual"
	var selected_identity: Dictionary = selection.identity()
	if selected_identity.get("gameplay_epoch") != gameplay_epoch \
			or selected_identity.get("gameplay_id") != gameplay_id:
		return "Godot selection did not retain the gameplay identity"
	if selection.apply_pick(masked, outline) or visual.material_overlay != previous_overlay:
		return "miss did not clear the outline and restore the previous overlay"
	if not selection.apply_pick(hit, outline):
		return "valid selection could not be reapplied"
	var external_overlay := StandardMaterial3D.new()
	visual.material_overlay = external_overlay
	selection.clear()
	if visual.material_overlay != external_overlay:
		return "clearing selection replaced a newer material overlay"
	if not selection.apply_pick(hit, outline):
		return "selection could not be restored before visual destruction"
	visual.queue_free()
	await parent.get_tree().process_frame
	if selection.identity().get("gameplay_epoch") != 0:
		return "destroyed visual retained a live selection"
	target.set_meta(ScenePicking.ENTITY_KEY, "malformed")
	var malformed := ScenePicking.pick(camera, center, COLLISION_MASK)
	if malformed.status != ScenePicking.Status.NOT_SELECTABLE or malformed.collider != target:
		return "malformed gameplay metadata was accepted"

	var blocker := _box_body(scene, "UnboundCollider",
		Vector3(0.0, 0.0, BLOCKER_DISTANCE), COLLISION_MASK)
	await parent.get_tree().physics_frame
	var blocked := ScenePicking.pick(camera, center, COLLISION_MASK)
	if blocked.status != ScenePicking.Status.NOT_SELECTABLE or blocked.collider != blocker:
		return "an unbound collider was selected or incorrectly passed through"
	scene.queue_free()
	return ""

static func _box_body(parent: Node3D, node_name: String, position: Vector3, layer: int) -> StaticBody3D:
	var body := StaticBody3D.new()
	body.name = node_name
	body.position = position
	body.collision_layer = layer
	var shape := CollisionShape3D.new()
	var box := BoxShape3D.new()
	box.size = BOX_SIZE
	shape.shape = box
	body.add_child(shape)
	parent.add_child(body)
	return body
