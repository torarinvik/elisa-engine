extends Node3D

# Scene-local Godot implementation of Elisa's light and environment controls.
# Descriptor vectors are expressed in this node's local space. Handles are
# opaque dictionaries so slot reuse and cross-service use can be rejected.

enum Status { OK, INVALID_DESCRIPTOR, CAPACITY, STALE_HANDLE, FOREIGN_HANDLE, UNSUPPORTED_SHADOW_RESOLUTION, UNSUPPORTED_RENDER_FEATURE }

const MAX_LIGHTS := 64
const _ZERO_VECTOR_SQUARED := 0.000001
const _MAX_INTENSITY := 1.0e30
const _MAX_DISTANCE := 1.0e6
const _SHADOW_RESOLUTIONS := [0, 16, 32, 64, 128, 256, 512, 1024, 2048]

var _owner_id: int
var _slots: Array[Dictionary] = []
var _environment_node: WorldEnvironment
var _sun: DirectionalLight3D

func _init() -> void:
	_owner_id = get_instance_id()
	for _index in range(MAX_LIGHTS):
		_slots.append({"generation": 1, "node": null})
	_environment_node = WorldEnvironment.new()
	_environment_node.name = "ElisaEnvironment"
	add_child(_environment_node)

func create_light(descriptor: Dictionary) -> Dictionary:
	var validation := _validate_light(descriptor)
	if validation != Status.OK:
		return {"error": validation, "handle": {}}
	var slot := _free_slot()
	if slot < 0:
		return {"error": Status.CAPACITY, "handle": {}}
	var node := _new_light(String(descriptor["kind"]))
	_apply_light(node, descriptor)
	node.name = "ElisaLight_%d" % slot
	add_child(node)
	_slots[slot]["node"] = node
	var handle := _make_handle(slot)
	return {"error": Status.OK, "handle": handle}

func update_light(handle: Dictionary, descriptor: Dictionary) -> Status:
	var validation := _validate_light(descriptor)
	if validation != Status.OK:
		return validation
	var slot := _handle_slot(handle)
	if slot < 0:
		return Status.FOREIGN_HANDLE if int(handle.get("owner", 0)) != _owner_id else Status.STALE_HANDLE
	var node := _slots[slot]["node"] as Light3D
	if node == null or not is_instance_valid(node):
		return Status.STALE_HANDLE
	if _node_kind(node) != String(descriptor["kind"]):
		return Status.INVALID_DESCRIPTOR
	_apply_light(node, descriptor)
	return Status.OK

func destroy_light(handle: Dictionary) -> Status:
	var slot := _handle_slot(handle)
	if slot < 0:
		return Status.FOREIGN_HANDLE if int(handle.get("owner", 0)) != _owner_id else Status.STALE_HANDLE
	var node := _slots[slot]["node"] as Light3D
	if node == null or not is_instance_valid(node):
		return Status.STALE_HANDLE
	remove_child(node)
	node.free()
	_slots[slot]["node"] = null
	_slots[slot]["generation"] = int(_slots[slot]["generation"]) + 1
	return Status.OK

func live_light_count() -> int:
	var count := 0
	for slot in _slots:
		count += 1 if slot["node"] != null and is_instance_valid(slot["node"]) else 0
	return count

func environment_node() -> WorldEnvironment:
	return _environment_node

func capabilities() -> Dictionary:
	var method := String(RenderingServer.get_current_rendering_method())
	return {
		"rendering_method": method,
		"rectangle_light_shadows": method != "gl_compatibility",
		"spot_inner_cone": false,
		"per_light_shadow_resolution": false,
		"panorama_sky": true,
		"panorama_rotation": false,
	}

func set_sun_shadows(enabled: bool) -> Status:
	if _sun == null or not is_instance_valid(_sun):
		return Status.INVALID_DESCRIPTOR
	_sun.shadow_enabled = enabled
	return Status.OK

func set_sun_shadow_bias(bias: float, normal_bias: float) -> Status:
	if _sun == null or not is_instance_valid(_sun) or not is_finite(bias) \
			or not is_finite(normal_bias) or bias < 0.0 or normal_bias < 0.0 \
			or bias > _MAX_DISTANCE or normal_bias > _MAX_DISTANCE:
		return Status.INVALID_DESCRIPTOR
	_sun.shadow_bias = bias
	_sun.shadow_normal_bias = normal_bias
	return Status.OK

func set_sun_cascade_distances(splits: Vector3, camera_far: float) -> Status:
	if _sun == null or not is_instance_valid(_sun) or not _finite_vector(splits) \
			or not is_finite(camera_far) or camera_far <= 0.0 or camera_far > _MAX_DISTANCE \
			or splits.x <= 0.0 or splits.x >= splits.y or splits.y >= splits.z \
			or splits.z >= camera_far:
		return Status.INVALID_DESCRIPTOR
	_sun.directional_shadow_mode = DirectionalLight3D.SHADOW_PARALLEL_4_SPLITS
	_sun.directional_shadow_max_distance = camera_far
	_sun.directional_shadow_split_1 = splits.x / camera_far
	_sun.directional_shadow_split_2 = (splits.y - splits.x) / camera_far
	_sun.directional_shadow_split_3 = (splits.z - splits.y) / camera_far
	return Status.OK

func set_environment(descriptor: Dictionary) -> Status:
	if descriptor.has("sky_rotation"):
		var rotation: Variant = descriptor["sky_rotation"]
		if typeof(rotation) != TYPE_VECTOR3 or not _finite_vector(rotation):
			return Status.INVALID_DESCRIPTOR
		if rotation != Vector3.ZERO:
			return Status.UNSUPPORTED_RENDER_FEATURE
	if not _environment_valid(descriptor):
		return Status.INVALID_DESCRIPTOR
	var environment := Environment.new()
	var texture: Variant = descriptor.get("sky_texture", null)
	if texture != null:
		if not texture is Texture2D:
			return Status.INVALID_DESCRIPTOR
		var sky_material := PanoramaSkyMaterial.new()
		sky_material.panorama = texture
		var sky := Sky.new()
		sky.sky_material = sky_material
		environment.background_mode = Environment.BG_SKY
		environment.sky = sky
		environment.background_energy_multiplier = float(descriptor["sky_exposure"])
	else:
		environment.background_mode = Environment.BG_COLOR
		environment.background_color = descriptor.get("background_color", Color(0.015, 0.02, 0.03))
		environment.background_energy_multiplier = 1.0
	var ambient: Vector3 = descriptor["ambient"]
	var ambient_energy := maxf(ambient.x, maxf(ambient.y, ambient.z))
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.ambient_light_color = Color.WHITE if ambient_energy <= 0.0 else Color(
		ambient.x / ambient_energy, ambient.y / ambient_energy, ambient.z / ambient_energy)
	environment.ambient_light_energy = ambient_energy
	environment.fog_enabled = bool(descriptor["fog_enabled"])
	environment.fog_light_color = _color(descriptor["fog_color"])
	environment.fog_depth_begin = float(descriptor["fog_start"])
	environment.fog_density = float(descriptor["fog_density"])
	environment.tonemap_exposure = float(descriptor.get("exposure", 1.0))
	var sun_color: Vector3 = descriptor["sun_color"]
	var sun_energy := maxf(sun_color.x, maxf(sun_color.y, sun_color.z))
	var next_sun_color := Color.WHITE if sun_energy <= 0.0 else Color(
		sun_color.x / sun_energy, sun_color.y / sun_energy, sun_color.z / sun_energy)
	var direction: Vector3 = _normalized(descriptor["sun_direction"])
	# Both resources are fully prepared before the live environment or sun are
	# changed, so rejected descriptors cannot leave half-applied weather state.
	if _sun == null or not is_instance_valid(_sun):
		_sun = DirectionalLight3D.new()
		_sun.name = "ElisaSun"
		add_child(_sun)
	_sun.light_color = next_sun_color
	_sun.light_energy = sun_energy
	_sun.position = Vector3.ZERO
	_sun.basis = _direction_basis(direction)
	_environment_node.environment = environment
	return Status.OK

func _free_slot() -> int:
	for index in range(_slots.size()):
		if _slots[index]["node"] == null:
			return index
	return -1

func _make_handle(slot: int) -> Dictionary:
	return {"owner": _owner_id, "slot": slot, "generation": int(_slots[slot]["generation"])}

func _handle_slot(handle: Dictionary) -> int:
	if int(handle.get("owner", 0)) != _owner_id:
		return -1
	var slot := int(handle.get("slot", -1))
	if slot < 0 or slot >= _slots.size():
		return -1
	if int(handle.get("generation", 0)) != int(_slots[slot]["generation"]):
		return -1
	return slot

func _new_light(kind: String) -> Light3D:
	match kind:
		"directional":
			return DirectionalLight3D.new()
		"point":
			return OmniLight3D.new()
		"spot":
			return SpotLight3D.new()
		"rectangle":
			return AreaLight3D.new()
	return OmniLight3D.new()

func _node_kind(node: Light3D) -> String:
	if node is DirectionalLight3D:
		return "directional"
	if node is OmniLight3D:
		return "point"
	if node is SpotLight3D:
		return "spot"
	return "rectangle" if node is AreaLight3D else "unknown"

func _apply_light(node: Light3D, descriptor: Dictionary) -> void:
	var kind: String = descriptor["kind"]
	var color: Vector3 = descriptor["color"]
	node.light_color = _color(color)
	node.light_energy = float(descriptor["intensity"])
	node.shadow_enabled = bool(descriptor["casts_shadow"])
	node.shadow_bias = float(descriptor["shadow_bias"])
	node.shadow_normal_bias = float(descriptor["shadow_normal_bias"])
	node.light_volumetric_fog_energy = float(descriptor["volumetric_boost"]) if descriptor["volumetrics_enabled"] else 0.0
	if kind != "point":
		node.basis = _direction_basis(descriptor["direction"])
	node.position = descriptor["position"]
	if node is OmniLight3D:
		(node as OmniLight3D).omni_range = float(descriptor["range"])
	elif node is SpotLight3D:
		(node as SpotLight3D).spot_range = float(descriptor["range"])
		(node as SpotLight3D).spot_angle = rad_to_deg(float(descriptor["outer_cone"]))
	elif node is AreaLight3D:
		(node as AreaLight3D).area_range = float(descriptor["range"])
		(node as AreaLight3D).area_size = Vector2(float(descriptor["rectangle_width"]), float(descriptor["rectangle_height"]))
		(node as AreaLight3D).area_attenuation = 2.0

func _validate_light(descriptor: Dictionary) -> Status:
	for key in ["kind", "color", "position", "direction", "intensity", "range",
			"inner_cone", "outer_cone", "casts_shadow", "shadow_resolution",
			"shadow_bias", "shadow_normal_bias", "rectangle_width", "rectangle_height",
			"volumetrics_enabled", "volumetric_boost"]:
		if not descriptor.has(key):
			return Status.INVALID_DESCRIPTOR
	var kind: Variant = descriptor["kind"]
	if typeof(kind) != TYPE_STRING or not ["directional", "point", "spot", "rectangle"].has(kind):
		return Status.INVALID_DESCRIPTOR
	for key in ["color", "position", "direction"]:
		if typeof(descriptor[key]) != TYPE_VECTOR3 or not _finite_vector(descriptor[key]):
			return Status.INVALID_DESCRIPTOR
	for key in ["intensity", "range", "inner_cone", "outer_cone", "shadow_bias",
			"shadow_normal_bias", "rectangle_width", "rectangle_height", "volumetric_boost"]:
		if not descriptor.has(key) or not _finite_number(descriptor[key]):
			return Status.INVALID_DESCRIPTOR
	if not typeof(descriptor["casts_shadow"]) == TYPE_BOOL or not typeof(descriptor["volumetrics_enabled"]) == TYPE_BOOL:
		return Status.INVALID_DESCRIPTOR
	if not typeof(descriptor["shadow_resolution"]) == TYPE_INT or not _SHADOW_RESOLUTIONS.has(descriptor["shadow_resolution"]):
		return Status.INVALID_DESCRIPTOR
	if descriptor["shadow_resolution"] != 0:
		return Status.UNSUPPORTED_SHADOW_RESOLUTION
	var color: Vector3 = descriptor["color"]
	var position: Vector3 = descriptor["position"]
	var direction: Vector3 = descriptor["direction"]
	var intensity := float(descriptor["intensity"])
	var light_range := float(descriptor["range"])
	var inner := float(descriptor["inner_cone"])
	var outer := float(descriptor["outer_cone"])
	var width := float(descriptor["rectangle_width"])
	var height := float(descriptor["rectangle_height"])
	var boost := float(descriptor["volumetric_boost"])
	if kind == "rectangle" and bool(descriptor["casts_shadow"]) \
			and String(RenderingServer.get_current_rendering_method()) == "gl_compatibility":
		return Status.UNSUPPORTED_RENDER_FEATURE
	if color.x < 0.0 or color.y < 0.0 or color.z < 0.0 or intensity < 0.0 or intensity > _MAX_INTENSITY:
		return Status.INVALID_DESCRIPTOR
	if maxf(absf(position.x), maxf(absf(position.y), absf(position.z))) > _MAX_DISTANCE or light_range <= 0.0 or light_range > _MAX_DISTANCE:
		return Status.INVALID_DESCRIPTOR
	if kind != "point" and direction.length_squared() <= _ZERO_VECTOR_SQUARED:
		return Status.INVALID_DESCRIPTOR
	if kind == "spot" and (inner < 0.0 or outer <= 0.0 or inner > outer or outer > PI / 2.0):
		return Status.INVALID_DESCRIPTOR
	if kind == "spot" and inner > 0.0:
		return Status.UNSUPPORTED_RENDER_FEATURE
	if kind != "spot" and (inner != 0.0 or outer != 0.0):
		return Status.INVALID_DESCRIPTOR
	if kind == "rectangle":
		if width <= 0.0 or height <= 0.0 or width > _MAX_DISTANCE or height > _MAX_DISTANCE:
			return Status.INVALID_DESCRIPTOR
	elif width != 0.0 or height != 0.0:
		return Status.INVALID_DESCRIPTOR
	if boost < 0.0 or boost > 8.0:
		return Status.INVALID_DESCRIPTOR
	if not _finite_number(descriptor["shadow_bias"]) or float(descriptor["shadow_bias"]) < 0.0:
		return Status.INVALID_DESCRIPTOR
	if not _finite_number(descriptor["shadow_normal_bias"]) or float(descriptor["shadow_normal_bias"]) < 0.0:
		return Status.INVALID_DESCRIPTOR
	return Status.OK

func _environment_valid(descriptor: Dictionary) -> bool:
	for key in ["sun_color", "sun_direction", "ambient", "sky_exposure", "fog_color",
			"fog_start", "fog_density", "fog_enabled"]:
		if not descriptor.has(key):
			return false
	for key in ["sun_color", "sun_direction", "ambient", "fog_color"]:
		if typeof(descriptor[key]) != TYPE_VECTOR3 or not _finite_vector(descriptor[key]):
			return false
	for key in ["sky_exposure", "fog_start", "fog_density"]:
		if not _finite_number(descriptor[key]) or float(descriptor[key]) < 0.0:
			return false
	if typeof(descriptor["fog_enabled"]) != TYPE_BOOL:
		return false
	if not _finite_number(descriptor.get("exposure", 1.0)) or float(descriptor.get("exposure", 1.0)) < 0.0:
		return false
	var sun_direction: Vector3 = descriptor["sun_direction"]
	var sun_color: Vector3 = descriptor["sun_color"]
	var ambient: Vector3 = descriptor["ambient"]
	var fog_color: Vector3 = descriptor["fog_color"]
	if sun_direction.length_squared() <= _ZERO_VECTOR_SQUARED:
		return false
	for value in [sun_color, ambient, fog_color]:
		if value.x < 0.0 or value.y < 0.0 or value.z < 0.0:
			return false
	if descriptor.has("background_color") and (typeof(descriptor["background_color"]) != TYPE_COLOR or not _finite_color(descriptor["background_color"])):
		return false
	if descriptor.has("sky_texture") and descriptor["sky_texture"] != null and not descriptor["sky_texture"] is Texture2D:
		return false
	return true

func _direction_basis(direction: Vector3) -> Basis:
	var normalized_direction := _normalized(direction)
	var up := Vector3.UP
	if absf(normalized_direction.dot(up)) > 0.999:
		up = Vector3.FORWARD
	return Basis.looking_at(normalized_direction, up)

func _normalized(value: Vector3) -> Vector3:
	var scale := maxf(absf(value.x), maxf(absf(value.y), absf(value.z)))
	if scale <= 0.0:
		return Vector3.ZERO
	var scaled := value / scale
	return scaled / sqrt(scaled.length_squared())

func _color(value: Vector3) -> Color:
	return Color(value.x, value.y, value.z, 1.0)

func _finite_vector(value: Vector3) -> bool:
	return is_finite(value.x) and is_finite(value.y) and is_finite(value.z)

func _finite_color(value: Color) -> bool:
	return is_finite(value.r) and is_finite(value.g) and is_finite(value.b) and is_finite(value.a)

func _finite_number(value: Variant) -> bool:
	return (typeof(value) == TYPE_FLOAT or typeof(value) == TYPE_INT) and is_finite(float(value)) and absf(float(value)) <= _MAX_INTENSITY
