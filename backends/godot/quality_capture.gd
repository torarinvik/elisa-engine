extends SceneTree

const QualityService = preload("res://quality_service.gd")

func _initialize() -> void:
	call_deferred("_capture")

func _capture() -> void:
	var arguments := OS.get_cmdline_user_args()
	if arguments.size() != 2:
		_fail("expected <low.png> <high.png>")
		return
	var scene := Node3D.new()
	root.add_child(scene)
	var camera_attributes := CameraAttributesPractical.new()
	var environment := _environment()
	var world_environment := WorldEnvironment.new()
	world_environment.environment = environment
	scene.add_child(world_environment)
	var camera := Camera3D.new()
	camera.position = Vector3(4.8, 3.6, 6.5)
	camera.attributes = camera_attributes
	scene.add_child(camera)
	camera.look_at(Vector3(0.0, 0.0, 0.0), Vector3.UP)
	camera.current = true
	_add_scene_geometry(scene)
	var light := DirectionalLight3D.new()
	light.rotation_degrees = Vector3(-42.0, -28.0, 0.0)
	light.light_energy = 1.1
	light.shadow_enabled = true
	scene.add_child(light)

	var service := QualityService.new()
	var low: Dictionary = service.apply_profile(_profile(false), root, environment, camera_attributes)
	if low["error"] != QualityService.Status.OK:
		_fail("Low profile application failed")
		return
	await _settle_frames()
	if not _save_frame(arguments[0]):
		_fail("Low profile capture could not be saved")
		return

	var high: Dictionary = service.apply_profile(_profile(true), root, environment, camera_attributes)
	if high["error"] != QualityService.Status.OK \
			or high["outcome"] != "upscaler_fallback" \
			or not high["unsupported"].has("upscaler"):
		_fail("High profile failed or did not expose its Compatibility fallback")
		return
	await _settle_frames()
	if not _save_frame(arguments[1]):
		_fail("High profile capture could not be saved")
		return
	scene.queue_free()
	await process_frame
	quit(0)

func _profile(high_quality: bool) -> Dictionary:
	return {
		"level": "high" if high_quality else "low",
		"tonemap": "aces" if high_quality else "reinhard",
		"upscaler": "fsr2" if high_quality else "none",
		"render_scale": 1.0,
		"bloom": high_quality,
		"bloom_threshold": 0.65 if high_quality else 1.0,
		"fxaa": false,
		"temporal_aa": false,
		"ambient_occlusion": high_quality,
		"screen_space_reflections": false,
		"fog": high_quality,
		"depth_effects": false,
		"shadow_quality": "high" if high_quality else "low",
		"sun_shadow_receiver_bias": 0.0,
	}

func _environment() -> Environment:
	var environment := Environment.new()
	environment.background_mode = Environment.BG_COLOR
	environment.background_color = Color(0.018, 0.025, 0.04)
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.ambient_light_color = Color(0.34, 0.39, 0.5)
	environment.ambient_light_energy = 0.65
	environment.fog_mode = Environment.FOG_MODE_DEPTH
	environment.fog_light_color = Color(0.29, 0.34, 0.46)
	environment.fog_density = 0.08
	environment.fog_depth_begin = 1.0
	environment.fog_depth_end = 9.0
	environment.glow_intensity = 1.4
	environment.glow_strength = 1.2
	environment.glow_bloom = 0.18
	environment.ssao_radius = 2.0
	environment.ssao_intensity = 2.0
	return environment

func _add_scene_geometry(scene: Node3D) -> void:
	var floor := MeshInstance3D.new()
	var floor_mesh := PlaneMesh.new()
	floor_mesh.size = Vector2(14.0, 14.0)
	floor.mesh = floor_mesh
	floor.position.y = -0.85
	var floor_material := StandardMaterial3D.new()
	floor_material.albedo_color = Color(0.24, 0.3, 0.4)
	floor_material.roughness = 0.78
	floor.material_override = floor_material
	scene.add_child(floor)

	var positions: Array[Vector3] = [Vector3(-1.25, -0.1, 0.0), Vector3(0.45, -0.05, 0.2)]
	var colors: Array[Color] = [Color(0.82, 0.19, 0.11), Color(0.14, 0.42, 0.87)]
	for index in range(positions.size()):
		var box := MeshInstance3D.new()
		var mesh := BoxMesh.new()
		mesh.size = Vector3(1.1, 1.35, 1.0)
		box.mesh = mesh
		box.position = positions[index]
		var material := StandardMaterial3D.new()
		material.albedo_color = colors[index]
		material.roughness = 0.5
		box.material_override = material
		scene.add_child(box)

	var beacon := MeshInstance3D.new()
	var beacon_mesh := SphereMesh.new()
	beacon_mesh.radius = 0.42
	beacon_mesh.height = 0.84
	beacon.mesh = beacon_mesh
	beacon.position = Vector3(1.6, 0.3, -0.1)
	var beacon_material := StandardMaterial3D.new()
	beacon_material.albedo_color = Color(1.0, 0.65, 0.18)
	beacon_material.emission_enabled = true
	beacon_material.emission = Color(1.0, 0.42, 0.06)
	beacon_material.emission_energy_multiplier = 5.0
	beacon.material_override = beacon_material
	scene.add_child(beacon)

func _settle_frames() -> void:
	for _frame in range(5):
		await process_frame
	await RenderingServer.frame_post_draw

func _save_frame(filename: String) -> bool:
	var frame := root.get_texture().get_image()
	return frame != null and frame.save_png(filename) == OK

func _fail(message: String) -> void:
	push_error(message)
	quit(1)
