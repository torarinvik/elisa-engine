extends SceneTree

const QualityService = preload("res://quality_service.gd")

const WARMUP_FRAMES := 12
const MEASURED_FRAMES := 60

func _initialize() -> void:
	call_deferred("_capture")

func _capture() -> void:
	var arguments := OS.get_cmdline_user_args()
	if arguments.size() != 3:
		_fail("expected <low.png> <high.png> <measurements.json>")
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
	var viewport := root.get_viewport()
	var measured_frames := MEASURED_FRAMES
	var high_upscaler := OS.get_environment("ELISA_GODOT_QUALITY_UPSCALER")
	if high_upscaler.is_empty():
		high_upscaler = "fsr2"
	if not ["none", "fsr1", "fsr2"].has(high_upscaler):
		_fail("ELISA_GODOT_QUALITY_UPSCALER must be none, fsr1, or fsr2")
		return
	var configured_samples := OS.get_environment("ELISA_GODOT_QUALITY_MEASURED_FRAMES")
	if not configured_samples.is_empty():
		if not configured_samples.is_valid_int() or int(configured_samples) < 3 or int(configured_samples) > 1000:
			_fail("ELISA_GODOT_QUALITY_MEASURED_FRAMES must be an integer from 3 through 1000")
			return
		measured_frames = int(configured_samples)
	var measure_gpu := OS.get_environment("ELISA_GODOT_QUALITY_GPU_TIMING") != "0"
	RenderingServer.viewport_set_measure_render_time(viewport.get_viewport_rid(), true)
	var low: Dictionary = service.apply_profile(_profile(false), root, environment, camera_attributes)
	if low["error"] != QualityService.Status.OK:
		_fail("Low profile application failed")
		return
	print("Godot quality capture: measuring Low profile")
	var low_metrics := await _measure_profile(viewport, measured_frames, measure_gpu)
	print("Godot quality capture: Low profile measured")
	if not _save_frame(arguments[0]):
		_fail("Low profile capture could not be saved")
		return

	var high: Dictionary = service.apply_profile(_profile(true, high_upscaler), root, environment, camera_attributes)
	var high_upscaler_supported: bool = service.capabilities().get(high_upscaler, high_upscaler == "none")
	if high["error"] != QualityService.Status.OK \
			or (high_upscaler_supported and high["unsupported"].has("upscaler")) \
			or (not high_upscaler_supported and (high["outcome"] != "upscaler_fallback" or not high["unsupported"].has("upscaler"))):
		_fail("High profile did not match the active renderer's upscaler capability")
		return
	print("Godot quality capture: measuring High profile")
	var high_metrics := await _measure_profile(viewport, measured_frames, measure_gpu)
	print("Godot quality capture: High profile measured")
	if not _save_frame(arguments[1]):
		_fail("High profile capture could not be saved")
		return
	# Medium runs after both captures so the Low/High images keep their references.
	var medium: Dictionary = service.apply_profile(_medium_profile(), root, environment, camera_attributes)
	if medium["error"] != QualityService.Status.OK:
		_fail("Medium profile application failed")
		return
	print("Godot quality capture: measuring Medium profile")
	var medium_metrics := await _measure_profile(viewport, measured_frames, measure_gpu)
	print("Godot quality capture: Medium profile measured")
	if not _save_measurements(arguments[2], low_metrics, medium_metrics, high_metrics, viewport, measured_frames, high_upscaler, measure_gpu):
		_fail("Godot profile measurements could not be saved")
		return
	scene.queue_free()
	await process_frame
	quit(0)

func _profile(high_quality: bool, high_upscaler: String = "fsr2") -> Dictionary:
	return {
		"level": "high" if high_quality else "low",
		"tonemap": "aces" if high_quality else "reinhard",
		"upscaler": high_upscaler if high_quality else "none",
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

# Matches Quality::Profile() defaults: bloom and fog on, no SSAO, medium shadows.
func _medium_profile() -> Dictionary:
	var profile := _profile(false)
	profile["level"] = "medium"
	profile["tonemap"] = "aces"
	profile["bloom"] = true
	profile["fog"] = true
	profile["shadow_quality"] = "medium"
	return profile

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

func _measure_profile(viewport: Viewport, measured_frames: int, measure_gpu: bool) -> Dictionary:
	var cpu_samples: Array[float] = []
	var gpu_samples: Array[float] = []
	for frame_index in range(WARMUP_FRAMES + measured_frames):
		await process_frame
		await RenderingServer.frame_post_draw
		if frame_index < WARMUP_FRAMES:
			continue
		var cpu_ms := RenderingServer.viewport_get_measured_render_time_cpu(viewport.get_viewport_rid())
		var gpu_ms := RenderingServer.viewport_get_measured_render_time_gpu(viewport.get_viewport_rid()) if measure_gpu else 0.0
		if cpu_ms > 0.0:
			cpu_samples.append(cpu_ms)
		if gpu_ms > 0.0:
			gpu_samples.append(gpu_ms)
	return {
		"cpu_ms": _summarize(cpu_samples),
		"gpu_ms": _summarize(gpu_samples),
		"video_memory_bytes": RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_VIDEO_MEM_USED),
	}

func _summarize(samples: Array[float]) -> Dictionary:
	if samples.is_empty():
		return {"sample_count": 0, "p50": null, "p95": null, "p99": null}
	var sorted := samples.duplicate()
	sorted.sort()
	return {
		"sample_count": sorted.size(),
		"p50": _percentile(sorted, 0.50),
		"p95": _percentile(sorted, 0.95),
		"p99": _percentile(sorted, 0.99),
	}

func _percentile(sorted: Array[float], percentile: float) -> float:
	var index := clampi(int(ceil(percentile * sorted.size())) - 1, 0, sorted.size() - 1)
	return sorted[index]

func _save_measurements(filename: String, low: Dictionary, medium: Dictionary, high: Dictionary, viewport: Viewport, measured_frames: int, high_upscaler: String, measure_gpu: bool) -> bool:
	var rendering_method := RenderingServer.get_current_rendering_method()
	var gpu_status := "unsupported_by_compatibility_renderer" if rendering_method == "gl_compatibility" else "unavailable_no_samples"
	if not measure_gpu:
		gpu_status = "disabled_by_configuration"
	if low["gpu_ms"]["sample_count"] > 0 and medium["gpu_ms"]["sample_count"] > 0 and high["gpu_ms"]["sample_count"] > 0:
		gpu_status = "available"
	var report := {
		"schema": 1,
		"godot_version": Engine.get_version_info()["string"],
		"rendering_method": rendering_method,
		"viewport_width": viewport.size.x,
		"viewport_height": viewport.size.y,
		"warmup_frames": WARMUP_FRAMES,
		"measured_frames": measured_frames,
		"high_upscaler": high_upscaler,
		"gpu_timing_status": gpu_status,
		"video_memory_scope": "renderer-wide allocation; includes shared resources and is not attributable to one profile",
		"profiles": {"low": low, "medium": medium, "high": high},
	}
	var file := FileAccess.open(filename, FileAccess.WRITE)
	if file == null:
		return false
	file.store_string(JSON.stringify(report, "\t") + "\n")
	return file.get_error() == OK

func _save_frame(filename: String) -> bool:
	var frame := root.get_texture().get_image()
	return frame != null and frame.save_png(filename) == OK

func _fail(message: String) -> void:
	push_error(message)
	quit(1)
