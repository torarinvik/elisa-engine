extends SceneTree

const ScenePicking = preload("res://scene_picking.gd")
const ElisaSelection = preload("res://editor_selection.gd")
const OutlineShader = preload("res://selection_outline.gdshader")
const BOX_SIZE := Vector3(2.0, 2.0, 2.0)
const CAMERA_POSITION := Vector3(2.0, 1.5, 7.0)
const OUTLINE_WIDTH := 0.08
const TEST_GAMEPLAY_EPOCH := 1
const TEST_GAMEPLAY_ID := 1
const BASE_COLOR := Color(0.12, 0.42, 0.82, 1.0)

func _initialize() -> void:
	call_deferred("_capture_outline")

func _capture_outline() -> void:
	var arguments := OS.get_cmdline_user_args()
	if arguments.size() != 2:
		_fail("expected <baseline.png> <outline.png>")
		return
	var scene := Node3D.new()
	root.add_child(scene)
	var environment := Environment.new()
	environment.background_mode = Environment.BG_COLOR
	environment.background_color = Color(0.02, 0.025, 0.04, 1.0)
	var world_environment := WorldEnvironment.new()
	world_environment.environment = environment
	scene.add_child(world_environment)
	var camera := Camera3D.new()
	camera.position = CAMERA_POSITION
	scene.add_child(camera)
	camera.look_at(Vector3.ZERO, Vector3.UP)
	camera.current = true
	var visual := MeshInstance3D.new()
	var mesh := BoxMesh.new()
	mesh.size = BOX_SIZE
	var base_material := StandardMaterial3D.new()
	base_material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	base_material.albedo_color = BASE_COLOR
	mesh.material = base_material
	visual.mesh = mesh
	scene.add_child(visual)
	await process_frame
	await process_frame
	await RenderingServer.frame_post_draw
	if not _save_frame(arguments[0]):
		_fail("baseline screenshot could not be written")
		return
	var outline := ShaderMaterial.new()
	outline.shader = OutlineShader
	outline.set_shader_parameter("outline_width", OUTLINE_WIDTH)
	var selection = ElisaSelection.new()
	var picked := {
		"status": ScenePicking.Status.HIT,
		"gameplay_epoch": TEST_GAMEPLAY_EPOCH,
		"gameplay_id": TEST_GAMEPLAY_ID,
		"visual": visual,
	}
	if not selection.apply_pick(picked, outline):
		_fail("outline material was rejected")
		return
	await process_frame
	await RenderingServer.frame_post_draw
	if not _save_frame(arguments[1]):
		_fail("outlined screenshot could not be written")
		return
	selection.clear()
	quit(0)

func _save_frame(filename: String) -> bool:
	var frame := root.get_texture().get_image()
	return frame != null and frame.save_png(filename) == OK

func _fail(message: String) -> void:
	push_error(message)
	quit(1)
