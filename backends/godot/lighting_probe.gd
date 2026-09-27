extends SceneTree

const ElisaLighting = preload("res://lighting_service.gd")

func _initialize() -> void:
	call_deferred("_run_probe")

func _run_probe() -> void:
	var service := ElisaLighting.new()
	var other_service := ElisaLighting.new()
	root.add_child(service)
	root.add_child(other_service)
	var point := _light("point")
	point["direction"] = Vector3.ZERO
	var point_result := service.create_light(point)
	if point_result["error"] != ElisaLighting.Status.OK:
		_fail("point light creation rejected a zero direction")
		return
	var point_handle: Dictionary = point_result["handle"]
	var point_node := _light_node(service, point_handle) as OmniLight3D
	if point_node == null or point_node.omni_range != 12.0 or point_node.shadow_enabled:
		_fail("point light properties did not reach the Godot node")
		return
	for kind in ["directional", "spot", "rectangle"]:
		var descriptor := _light(kind)
		var result := service.create_light(descriptor)
		if result["error"] != ElisaLighting.Status.OK:
			_fail("%s light creation failed" % kind)
			return
		var node := _light_node(service, result["handle"])
		if node == null or not is_instance_valid(node):
			_fail("%s light was not parented under its service" % kind)
			return
		if kind == "directional":
			if not node is DirectionalLight3D or not _close_vector(-(node as DirectionalLight3D).basis.z, Vector3.DOWN):
				_fail("directional light orientation was not preserved")
				return
		elif kind == "spot":
			if not node is SpotLight3D or not is_equal_approx((node as SpotLight3D).spot_angle, rad_to_deg(0.8)):
				_fail("spot light cone was not converted to Godot degrees")
				return
		else:
			if not node is AreaLight3D or (node as AreaLight3D).area_size != Vector2(2.0, 3.0):
				_fail("rectangular light dimensions were not applied")
				return
	if service.live_light_count() != 4:
		_fail("light count does not match allocated resources")
	var capabilities: Dictionary = service.capabilities()
	if capabilities["per_light_shadow_resolution"] or not capabilities["panorama_sky"] \
			or capabilities["spot_inner_cone"] \
			or capabilities["panorama_rotation"]:
		_fail("Godot lighting capability report is inconsistent")
		return
	if capabilities["rendering_method"] == "gl_compatibility":
		var unsupported_area_shadow := _light("rectangle")
		unsupported_area_shadow["casts_shadow"] = true
		if service.create_light(unsupported_area_shadow)["error"] != ElisaLighting.Status.UNSUPPORTED_RENDER_FEATURE \
				or service.live_light_count() != 4:
			_fail("Compatibility renderer accepted unsupported area-light shadows")
			return
	var unsupported_inner_cone := _light("spot")
	unsupported_inner_cone["inner_cone"] = 0.2
	if service.create_light(unsupported_inner_cone)["error"] != ElisaLighting.Status.UNSUPPORTED_RENDER_FEATURE \
			or service.live_light_count() != 4:
		_fail("unsupported spot inner cone was silently discarded")
		return
	var invalid_update := _light("point")
	invalid_update["intensity"] = -1.0
	if service.update_light(point_handle, invalid_update) != ElisaLighting.Status.INVALID_DESCRIPTOR \
			or not is_equal_approx(point_node.light_energy, 2.5):
		_fail("rejected light update changed the live light")
		return
	var missing_bias := _light("point")
	missing_bias.erase("shadow_bias")
	if service.update_light(point_handle, missing_bias) != ElisaLighting.Status.INVALID_DESCRIPTOR \
			or not is_equal_approx(point_node.light_energy, 2.5):
		_fail("incomplete light descriptor changed the live light")
		return
	if service.update_light(point_handle, _light("point")) != ElisaLighting.Status.OK:
		_fail("valid light update failed")
	var foreign := other_service.update_light(point_handle, _light("point"))
	if foreign != ElisaLighting.Status.FOREIGN_HANDLE:
		_fail("foreign light handle was accepted")
	var resolution_override := _light("point")
	resolution_override["shadow_resolution"] = 512
	if service.update_light(point_handle, resolution_override) != ElisaLighting.Status.UNSUPPORTED_SHADOW_RESOLUTION:
		_fail("unsupported per-light shadow resolution was silently accepted")
		return
	var sky_texture := GradientTexture2D.new()
	var weather := _environment(sky_texture)
	if service.set_environment(weather) != ElisaLighting.Status.OK:
		_fail("valid sky environment was rejected")
		return
	var world_environment := service.environment_node().environment
	var sky := world_environment.sky as Sky
	var sky_material := sky.sky_material as PanoramaSkyMaterial
	var sun := _sun_node(service)
	if world_environment.background_mode != Environment.BG_SKY or sky_material.panorama != sky_texture \
			or not is_equal_approx(world_environment.background_energy_multiplier, 0.75) \
			or not is_equal_approx(world_environment.tonemap_exposure, 1.25):
		_fail("sky or camera exposure settings did not reach the environment")
		return
	if not world_environment.fog_enabled or world_environment.fog_light_color != Color(0.3, 0.4, 0.5) \
			or not is_equal_approx(world_environment.fog_depth_begin, 8.0) \
			or not is_equal_approx(world_environment.fog_density, 0.04):
		_fail("fog settings did not reach the environment")
		return
	if sun == null or not _close_vector(-(sun.basis.z), Vector3.DOWN) or sun.light_color != Color(1.0, 0.8, 0.4) \
			or not is_equal_approx(sun.light_energy, 4.0):
		_fail("sun color, intensity, or direction was not applied")
		return
	if service.set_sun_shadows(true) != ElisaLighting.Status.OK or not sun.shadow_enabled \
			or service.set_sun_shadow_bias(0.002, 0.75) != ElisaLighting.Status.OK \
			or not is_equal_approx(sun.shadow_bias, 0.002) or not is_equal_approx(sun.shadow_normal_bias, 0.75):
		_fail("sun shadow enable or bias controls did not reach the light")
		return
	if service.set_sun_shadow_bias(-0.1, 0.75) != ElisaLighting.Status.INVALID_DESCRIPTOR \
			or not is_equal_approx(sun.shadow_bias, 0.002):
		_fail("invalid sun shadow bias partially changed the live light")
		return
	if service.set_sun_cascade_distances(Vector3(5.0, 15.0, 35.0), 50.0) != ElisaLighting.Status.OK \
			or sun.directional_shadow_mode != DirectionalLight3D.SHADOW_PARALLEL_4_SPLITS \
			or not is_equal_approx(sun.directional_shadow_max_distance, 50.0) \
			or not is_equal_approx(sun.directional_shadow_split_1, 0.1) \
			or not is_equal_approx(sun.directional_shadow_split_2, 0.2) \
			or not is_equal_approx(sun.directional_shadow_split_3, 0.4):
		_fail("sun cascade distances were not converted to Godot's normalized split settings")
		return
	if service.set_sun_cascade_distances(Vector3(20.0, 10.0, 30.0), 50.0) != ElisaLighting.Status.INVALID_DESCRIPTOR \
			or not is_equal_approx(sun.directional_shadow_split_1, 0.1):
		_fail("invalid cascade distances changed the live sun")
		return
	var invalid_weather := _environment(sky_texture)
	invalid_weather["fog_density"] = -0.1
	if service.set_environment(invalid_weather) != ElisaLighting.Status.INVALID_DESCRIPTOR \
			or service.environment_node().environment != world_environment \
			or not is_equal_approx(sun.light_energy, 4.0):
		_fail("rejected environment update changed the active environment")
		return
	var unrotatable_sky := _environment(sky_texture)
	unrotatable_sky["sky_rotation"] = Vector3(0.0, 1.0, 0.0)
	if service.set_environment(unrotatable_sky) != ElisaLighting.Status.UNSUPPORTED_RENDER_FEATURE \
			or service.environment_node().environment != world_environment:
		_fail("unsupported panorama rotation was not rejected without mutation")
		return
	var color_environment := _environment(null)
	if service.set_environment(color_environment) != ElisaLighting.Status.OK \
			or service.environment_node().environment.background_mode != Environment.BG_COLOR:
		_fail("color background environment failed")
		return
	var old_point_handle := point_handle.duplicate()
	if service.destroy_light(point_handle) != ElisaLighting.Status.OK:
		_fail("light destruction failed")
		return
	var replacement := service.create_light(_light("point"))
	if replacement["error"] != ElisaLighting.Status.OK \
			or int(replacement["handle"]["generation"]) == int(old_point_handle["generation"]) \
			or service.destroy_light(old_point_handle) != ElisaLighting.Status.STALE_HANDLE:
		_fail("reused light slot accepted a stale handle")
		return
	if service.live_light_count() != 4:
		_fail("light destruction and slot reuse count is incorrect")
	for _index in range(ElisaLighting.MAX_LIGHTS - service.live_light_count()):
		if service.create_light(_light("point"))["error"] != ElisaLighting.Status.OK:
			_fail("light capacity rejected a valid slot")
			return
	if service.live_light_count() != ElisaLighting.MAX_LIGHTS \
			or service.create_light(_light("point"))["error"] != ElisaLighting.Status.CAPACITY \
			or service.live_light_count() != ElisaLighting.MAX_LIGHTS:
		_fail("light capacity overflow changed the live table")
		return
	service.queue_free()
	other_service.queue_free()
	await process_frame
	if is_instance_valid(service) or is_instance_valid(other_service):
		_fail("lighting owners did not release their scene nodes")
		return
	print("Godot light handles, directional/point/spot/area nodes, environment, sky, fog, exposure, and atomic updates passed.")
	quit(0)

func _light(kind: String) -> Dictionary:
	return {
		"kind": kind,
		"color": Vector3(1.0, 0.8, 0.4),
		"position": Vector3(1.0, 2.0, 3.0),
		"direction": Vector3.DOWN,
		"intensity": 2.5,
		"range": 12.0,
		"inner_cone": 0.0,
		"outer_cone": 0.8 if kind == "spot" else 0.0,
		"casts_shadow": kind != "point" and kind != "rectangle",
		"shadow_resolution": 0,
		"shadow_bias": 0.04,
		"shadow_normal_bias": 1.0,
		"rectangle_width": 2.0 if kind == "rectangle" else 0.0,
		"rectangle_height": 3.0 if kind == "rectangle" else 0.0,
		"volumetrics_enabled": true,
		"volumetric_boost": 1.5,
	}

func _environment(sky_texture: Texture2D) -> Dictionary:
	var descriptor := {
		"sun_color": Vector3(4.0, 3.2, 1.6),
		"sun_direction": Vector3.DOWN,
		"ambient": Vector3(0.2, 0.3, 0.4),
		"sky_exposure": 0.75,
		"fog_color": Vector3(0.3, 0.4, 0.5),
		"fog_start": 8.0,
		"fog_density": 0.04,
		"fog_enabled": true,
		"exposure": 1.25,
	}
	if sky_texture != null:
		descriptor["sky_texture"] = sky_texture
	return descriptor

func _light_node(service: Node, handle: Dictionary) -> Light3D:
	var slot := int(handle["slot"])
	for child in service.get_children():
		if child is Light3D and child.name == "ElisaLight_%d" % slot:
			return child as Light3D
	return null

func _sun_node(service: Node) -> DirectionalLight3D:
	for child in service.get_children():
		if child.name == "ElisaSun" and child is DirectionalLight3D:
			return child as DirectionalLight3D
	return null

func _close_vector(left: Vector3, right: Vector3) -> bool:
	return left.distance_to(right) < 0.0001

func _fail(message: String) -> void:
	push_error(message)
	quit(1)
