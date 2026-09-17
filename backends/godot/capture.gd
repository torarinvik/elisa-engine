extends SceneTree

# Rendered-output capture for the Godot host. This is the non-headless
# sibling of probe.gd: --headless forces Godot's dummy rendering driver, so
# only a real display driver produces pixels. It consumes the same portable
# scene manifest as the native host, captures the root viewport to PNG, and
# exits. The Elisa world stays authoritative; this is a host-side
# representation only.

func _initialize() -> void:
    call_deferred("_run_capture")

func _run_capture() -> void:
    var arguments := OS.get_cmdline_user_args()
    if arguments.size() != 2:
        _fail("expected <scene-manifest> <output-png>")
        return
    var manifest := _load_manifest(arguments[0])
    if manifest.is_empty():
        return
    var object_position := Vector3(
        float(manifest["object_x"]), float(manifest["object_y"]), float(manifest["object_z"])
    )
    var camera_position := Vector3(
        float(manifest["camera_x"]), float(manifest["camera_y"]), float(manifest["camera_z"])
    )

    var scene_root := Node3D.new()
    scene_root.name = "ElisaScene"
    root.add_child(scene_root)

    # Black background to match the native host's clearing colour: a
    # cross-backend pixel comparison must compare renderers, not two
    # different default backgrounds.
    var environment := Environment.new()
    environment.background_mode = Environment.BG_COLOR
    environment.background_color = Color(0.0, 0.0, 0.0, 1.0)
    environment.ambient_light_source = Environment.AMBIENT_SOURCE_DISABLED
    var environment_node := WorldEnvironment.new()
    environment_node.name = "ElisaEnvironment"
    environment_node.environment = environment
    scene_root.add_child(environment_node)

    # Maze wall geometry from the Elisa topology, same placement rule as the
    # native host: an 8x8 map on a vertical plane 6 units from the camera.
    var wall_count := 0
    if manifest.has("walls"):
        for cell in String(manifest["walls"]).split(";"):
            if cell.is_empty():
                continue
            var parts := cell.split(",")
            if parts.size() != 2:
                _fail("wall cell malformed")
                return
            var cell_x := int(parts[0])
            var cell_y := int(parts[1])
            var wall := MeshInstance3D.new()
            wall.name = "ElisaWall_%d_%d" % [cell_x, cell_y]
            var wall_mesh := BoxMesh.new()
            wall_mesh.size = Vector3(0.6, 0.6, 0.6)
            var wall_material := StandardMaterial3D.new()
            wall_material.albedo_color = Color(0.8, 0.8, 0.85, 1.0)
            wall_material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
            wall_mesh.material = wall_material
            wall.mesh = wall_mesh
            wall.position = Vector3(cell_x * 0.6 - 2.1, cell_y * 0.6 - 2.1, 1.0)
            scene_root.add_child(wall)
            wall_count += 1

    var object := MeshInstance3D.new()
    object.name = "ElisaObject"
    var object_mesh := BoxMesh.new()
    object_mesh.size = Vector3(2.0, 2.0, 2.0)
    var object_material := StandardMaterial3D.new()
    object_material.albedo_color = Color(0.2, 0.7, 1.0, 1.0)
    object_material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
    object_mesh.material = object_material
    object.mesh = object_mesh
    object.position = object_position
    scene_root.add_child(object)

    var camera := Camera3D.new()
    camera.name = "ElisaCamera"
    camera.position = camera_position
    # Pin the vertical field of view to the native host's 45 degrees so the
    # two hosts project the same scene to the same screen area; otherwise a
    # cross-backend pixel comparison compares camera setups, not renderers.
    camera.fov = 45.0
    camera.near = 0.01
    scene_root.add_child(camera)
    camera.look_at(Vector3(0.0, 0.0, 3.0), Vector3.UP)
    camera.current = true

    # Let the renderer produce real frames before reading the viewport back.
    for _frame in range(8):
        await process_frame

    var image: Image = root.get_texture().get_image()
    if image == null:
        _fail("viewport image unavailable")
        return
    var status := image.save_png(arguments[1])
    if status != OK:
        _fail("capture save failed")
        return
    var visible_walls := 0
    for child in scene_root.get_children():
        if child is MeshInstance3D and child.name.begins_with("ElisaWall"):
            visible_walls += 1
    print("godot capture: %dx%d walls=%d" % [image.get_width(), image.get_height(), visible_walls])
    quit(0)

func _fail(message: String) -> void:
    push_error(message)
    quit(1)

func _load_manifest(filename: String) -> Dictionary:
    if not FileAccess.file_exists(filename):
        _fail("scene manifest is missing")
        return {}
    var file := FileAccess.open(filename, FileAccess.READ)
    if file == null:
        _fail("scene manifest cannot be opened")
        return {}
    var values := {}
    for line in file.get_as_text().split("\n"):
        var text := line.strip_edges()
        if text.is_empty() or text.begins_with("#"):
            continue
        var separator := text.find("=")
        if separator < 1:
            _fail("scene manifest line is malformed")
            return {}
        values[text.left(separator)] = text.substr(separator + 1).strip_edges()
    if values.get("version", "") != "1":
        _fail("scene manifest version mismatch")
        return {}
    return values
