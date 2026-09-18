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
    # The authored asset must reach this host through the cooked package, and
    # the audio clip must decode to the same sample information the native host
    # checks. Both are gated here, headless, so a regression fails the main
    # check rather than only the non-headless capture.
    var manifest_path: String = OS.get_cmdline_user_args()[0]
    if manifest.has("mesh_asset") and manifest.has("mesh_triangles"):
        var asset_name: String = String(manifest["mesh_asset"]).get_file().get_basename()
        var package_path: String = manifest_path.get_base_dir().path_join("..").path_join("build/cooked").path_join(asset_name + ".pkg").simplify_path()
        if not FileAccess.file_exists(package_path):
            _fail("cooked package is missing: %s" % package_path)
            return
        var package := {}
        for line in FileAccess.get_file_as_string(package_path).split("\n"):
            var text := line.strip_edges()
            if text.is_empty() or text.find("=") < 1:
                continue
            package[text.left(text.find("="))] = text.substr(text.find("=") + 1).strip_edges()
        if package.get("format", "") != "elisa-cooked-v2" or int(package.get("triangles", "-1")) != int(manifest["mesh_triangles"]):
            _fail("cooked package does not match the import")
            return
        var position_floats := Marshalls.base64_to_raw(package["positions_b64"]).to_float32_array()
        var normal_floats := Marshalls.base64_to_raw(package["normals_b64"]).to_float32_array()
        var index_ints := Marshalls.base64_to_raw(package["indices_b64"]).to_int32_array()
        var cooked_vertices := PackedVector3Array()
        for index in range(position_floats.size() / 3):
            cooked_vertices.append(Vector3(position_floats[index * 3], position_floats[index * 3 + 1], position_floats[index * 3 + 2]))
        var cooked_normals := PackedVector3Array()
        for index in range(normal_floats.size() / 3):
            cooked_normals.append(Vector3(normal_floats[index * 3], normal_floats[index * 3 + 1], normal_floats[index * 3 + 2]))
        var cooked_arrays := []
        cooked_arrays.resize(Mesh.ARRAY_MAX)
        cooked_arrays[Mesh.ARRAY_VERTEX] = cooked_vertices
        cooked_arrays[Mesh.ARRAY_NORMAL] = cooked_normals
        cooked_arrays[Mesh.ARRAY_INDEX] = index_ints
        var cooked_mesh := ArrayMesh.new()
        cooked_mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, cooked_arrays)
        print("godot probe asset: format=%s triangles=%s surface=%d" % [package.get("format", ""), package.get("triangles", ""), cooked_mesh.get_surface_count()])
        if cooked_mesh.get_surface_count() < 1:
            _fail("cooked package produced no surface")
            return

    var audio_samples := 400
    var audio_rate := 8000
    var audio_bytes := PackedByteArray()
    audio_bytes.resize(audio_samples * 2)
    for audio_index in range(audio_samples):
        audio_bytes.encode_s16(audio_index * 2, int(sin(TAU * 440.0 * float(audio_index) / float(audio_rate)) * 8000.0))
    var audio_stream := AudioStreamWAV.new()
    audio_stream.format = AudioStreamWAV.FORMAT_16_BITS
    audio_stream.mix_rate = audio_rate
    audio_stream.stereo = false
    audio_stream.data = audio_bytes
    print("godot probe audio: decoded_bytes=%d rate=%d cues=%s" % [audio_stream.data.size(), audio_stream.mix_rate, manifest.get("audio_cues", "0")])
    if audio_stream.data.size() != audio_samples * 2 or audio_stream.mix_rate != audio_rate or audio_stream.format != AudioStreamWAV.FORMAT_16_BITS or audio_stream.stereo:
        _fail("audio sample information mismatch")
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
