extends SceneTree

# This probe models the small command contract emitted by the headless recorder
# and applies it to real Godot scene resources. The Elisa world remains the
# authority; Godot only owns this host-side representation.
func _initialize() -> void:
    call_deferred("_run_probe")

func _run_probe() -> void:
    var manifest := _load_manifest()
    if manifest.is_empty():
        return
    var expected_epoch: int = int(manifest["epoch"])
    var expected_entity: int = int(manifest["entity"])
    var object_position := Vector3(
        float(manifest["object_x"]), float(manifest["object_y"]), float(manifest["object_z"])
    )
    var camera_position := Vector3(
        float(manifest["camera_x"]), float(manifest["camera_y"]), float(manifest["camera_z"])
    )
    var commands: Array[Dictionary] = []
    for command_name in String(manifest["commands"]).split(","):
        commands.append({"kind": command_name, "epoch": expected_epoch, "entity": expected_entity})

    var scene_root := Node3D.new()
    scene_root.name = "ElisaScene"
    root.add_child(scene_root)

    var object := MeshInstance3D.new()
    object.name = "ElisaCube_%d" % expected_entity
    var mesh := BoxMesh.new()
    mesh.size = Vector3(2.0, 2.0, 2.0)
    var material := StandardMaterial3D.new()
    material.albedo_color = Color(0.2, 0.7, 1.0, 1.0)
    mesh.material = material
    object.mesh = mesh
    scene_root.add_child(object)

    var camera := Camera3D.new()
    camera.name = "ElisaCamera_%d" % int(manifest["camera_entity"])
    camera.position = camera_position
    scene_root.add_child(camera)
    camera.look_at(object_position, Vector3.UP)
    camera.current = true

    var live := {}
    var updates := 0
    for command in commands:
        if command.epoch != expected_epoch or command.entity != expected_entity:
            _fail("command identity mismatch")
            return
        match command.kind:
            "create":
                if live.has(command.entity):
                    _fail("duplicate create")
                    return
                live[command.entity] = true
            "update":
                if not live.has(command.entity):
                    _fail("update before create")
                    return
                object.position = object_position
                if object.position != object_position:
                    _fail("scene transform mismatch")
                    return
                updates += 1
            "destroy":
                if not live.has(command.entity):
                    _fail("destroy before create")
                    return
                live.erase(command.entity)
                object.queue_free()
                camera.queue_free()
            _:
                _fail("unknown command")
                return
    if updates != 1 or not live.is_empty() or object.mesh == null or mesh.material == null:
        _fail("command lifecycle mismatch")
        return
    await process_frame
    if is_instance_valid(object) or is_instance_valid(camera):
        _fail("scene resource despawn mismatch")
        return
    print("Godot backend scene create/update/render/despawn probe passed.")
    quit(0)

func _fail(message: String) -> void:
    push_error(message)
    quit(1)

func _load_manifest() -> Dictionary:
    var arguments := OS.get_cmdline_user_args()
    if arguments.size() != 1:
        _fail("expected one scene manifest argument")
        return {}
    var filename: String = arguments[0]
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
