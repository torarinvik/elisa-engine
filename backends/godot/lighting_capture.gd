extends SceneTree

const ElisaLighting = preload("res://lighting_service.gd")

func _initialize() -> void:
	call_deferred("_capture")

func _capture() -> void:
	var arguments := OS.get_cmdline_user_args()
	if arguments.size() != 3:
		_fail("expected <unlit.png> <lit.png> <moved.png>")
		return
	var scene := Node3D.new()
	root.add_child(scene)
	var service := ElisaLighting.new()
	scene.add_child(service)
	var camera := Camera3D.new()
	camera.position = Vector3(4.0, 3.0, 6.0)
	scene.add_child(camera)
	camera.look_at(Vector3(0.0, 0.1, 0.0), Vector3.UP)
	camera.current = true
	var floor := MeshInstance3D.new()
	var floor_mesh := PlaneMesh.new()
	floor_mesh.size = Vector2(10.0, 10.0)
	var floor_material := StandardMaterial3D.new()
	floor_material.albedo_color = Color(0.48, 0.5, 0.54)
	floor_material.roughness = 0.85
	floor.mesh = floor_mesh
	floor.material_override = floor_material
	floor.position.y = -0.6
	scene.add_child(floor)
	var box := MeshInstance3D.new()
	var box_mesh := BoxMesh.new()
	box_mesh.size = Vector3(1.4, 1.2, 1.2)
	var box_material := StandardMaterial3D.new()
	box_material.albedo_color = Color(0.82, 0.26, 0.12)
	box_material.roughness = 0.55
	box.mesh = box_mesh
	box.material_override = box_material
	box.position.y = 0.0
	scene.add_child(box)
	var unlit_environment := _environment(false)
	if service.set_environment(unlit_environment) != ElisaLighting.Status.OK:
		_fail("unlit environment setup failed")
		return
	await _settle_frames()
	if not _save_frame(arguments[0]):
		_fail("unlit capture could not be saved")
		return
	if service.set_environment(_environment(true)) != ElisaLighting.Status.OK:
		_fail("lit environment setup failed")
		return
	var point := _point(Vector3(-2.0, 1.0, 2.0))
	var created := service.create_light(point)
	if created["error"] != ElisaLighting.Status.OK:
		_fail("point light creation failed")
		return
	var handle: Dictionary = created["handle"]
	await _settle_frames()
	if not _save_frame(arguments[1]):
		_fail("lit capture could not be saved")
		return
	point["position"] = Vector3(2.0, 1.0, 2.0)
	if service.update_light(handle, point) != ElisaLighting.Status.OK:
		_fail("point light movement failed")
		return
	await _settle_frames()
	if not _save_frame(arguments[2]):
		_fail("moved-light capture could not be saved")
		return
	service.queue_free()
	scene.queue_free()
	await process_frame
	quit(0)

func _environment(lit: bool) -> Dictionary:
	return {
		"sun_color": Vector3(1.1, 1.0, 0.9) if lit else Vector3.ZERO,
		"sun_direction": Vector3(-0.5, -1.0, -0.35),
		"ambient": Vector3(0.18, 0.18, 0.2) if lit else Vector3.ZERO,
		"sky_exposure": 0.0,
		"background_color": Color(0.015, 0.02, 0.03),
		"fog_color": Vector3(0.2, 0.23, 0.28),
		"fog_start": 10.0,
		"fog_density": 0.0,
		"fog_enabled": false,
		"exposure": 1.0,
	}

func _point(position: Vector3) -> Dictionary:
	return {
		"kind": "point",
		"color": Vector3(0.3, 0.55, 1.0),
		"position": position,
		"direction": Vector3.ZERO,
		"intensity": 64.0,
		"range": 10.0,
		"inner_cone": 0.0,
		"outer_cone": 0.0,
		"casts_shadow": false,
		"shadow_resolution": 0,
		"shadow_bias": 0.04,
		"shadow_normal_bias": 1.0,
		"rectangle_width": 0.0,
		"rectangle_height": 0.0,
		"volumetrics_enabled": false,
		"volumetric_boost": 0.0,
	}

func _settle_frames() -> void:
	await process_frame
	await process_frame
	await RenderingServer.frame_post_draw

func _save_frame(filename: String) -> bool:
	var frame := root.get_texture().get_image()
	return frame != null and frame.save_png(filename) == OK

func _fail(message: String) -> void:
	push_error(message)
	quit(1)
