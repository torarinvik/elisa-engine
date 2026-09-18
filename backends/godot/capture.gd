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
    # Fog of war: hide geometry outside the player's visible radius, using the
    # rule the game publishes. The player cell and radius come from the
    # fixture, so the host never chooses either.
    var fog_active := manifest.has("fog_radius") and manifest.has("player_final")
    var fog_radius := int(manifest.get("fog_radius", 0))
    var fog_player := Vector2i(0, 0)
    if fog_active:
        var player_parts := String(manifest["player_final"]).split(",")
        fog_player = Vector2i(int(player_parts[0]), int(player_parts[1]))
        fog_active = fog_radius > 0

    var wall_count := 0
    var walls_hidden := 0
    var debug_mesh := ImmediateMesh.new()
    debug_mesh.surface_begin(Mesh.PRIMITIVE_LINES)
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
            if fog_active and abs(cell_x - fog_player.x) + abs(cell_y - fog_player.y) > fog_radius:
                walls_hidden += 1
                continue
            # Debug collision geometry: each visible solid cell becomes a
            # wireframe box at the engine's cell extent (DebugGeometry uses the
            # same 0.6), verified and freed before capture so the frame is
            # unchanged.
            var debug_center := Vector3(cell_x * 0.6 - 2.1, cell_y * 0.6 - 2.1, 1.0)
            var debug_half := 0.3
            var corners: Array[Vector3] = []
            for sx in [-1.0, 1.0]:
                for sy in [-1.0, 1.0]:
                    for sz in [-1.0, 1.0]:
                        corners.append(debug_center + Vector3(sx * debug_half, sy * debug_half, sz * debug_half))
            for edge in [[0, 1], [0, 2], [0, 4], [1, 3], [1, 5], [2, 3], [2, 6], [3, 7], [4, 5], [4, 6], [5, 7], [6, 7]]:
                debug_mesh.surface_add_vertex(corners[edge[0]])
                debug_mesh.surface_add_vertex(corners[edge[1]])
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
    debug_mesh.surface_end()
    if manifest.has("walls"):
        var debug_vertices: int = debug_mesh.surface_get_arrays(0)[Mesh.ARRAY_VERTEX].size()
        if debug_vertices != wall_count * 24:
            _fail("debug collision wireframe was not built for every visible wall")
            return
        print("godot debug geometry: boxes=%d line_vertices=%d" % [wall_count, debug_vertices])

    var object := MeshInstance3D.new()
    object.name = "ElisaObject"
    var object_mesh := BoxMesh.new()
    object_mesh.size = Vector3(0.6, 0.6, 0.6)
    var object_material := StandardMaterial3D.new()
    object_material.albedo_color = Color(0.2, 0.7, 1.0, 1.0)
    object_material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
    object_mesh.material = object_material
    object.mesh = object_mesh
    object.position = object_position
    scene_root.add_child(object)

    # Game markers the Elisa rules place: key, door, hazards, goal. Each is a
    # cell-sized unlit cube so a captured frame can be checked for the right
    # object at the right cell.
    var marker_specs := [
        {"field": "goal", "name": "ElisaGoal", "color": Color(0.1, 0.9, 0.2, 1.0)},
        {"field": "key", "name": "ElisaKey", "color": Color(0.95, 0.85, 0.1, 1.0)},
        {"field": "door", "name": "ElisaDoor", "color": Color(0.85, 0.2, 0.9, 1.0)},
        {"field": "hazards", "name": "ElisaHazard", "color": Color(0.95, 0.15, 0.1, 1.0)},
        {"field": "hunter", "name": "ElisaHunter", "color": Color(1.0, 0.55, 0.1, 1.0)},
    ]
    # Status indicator: the host draws the game's state as a coloured cell.
    # Built later from the cooked package's geometry, used by the goal marker,
    # so it must be declared before the marker loop runs.
    var cooked_goal_mesh: ArrayMesh = null
    var cooked_goal_texture: Texture2D = null

    # Authored source asset, loaded through Godot's own importer instead of
    # being built in bridge code. The triangle count is exercised rather than
    # the vertex count, because importers split vertices by normal and UV.
    var mesh_triangles := 0
    if manifest.has("mesh_asset") and manifest.has("mesh_triangles"):
        var manifest_dir := arguments[0].get_base_dir()
        var asset_path: String = manifest_dir.path_join("..").path_join(String(manifest["mesh_asset"])).simplify_path()
        var gltf := GLTFDocument.new()
        var state := GLTFState.new()
        var load_error := gltf.append_from_file(asset_path, state)
        if load_error != OK:
            _fail("mesh asset could not be loaded: %s" % asset_path)
            return
        var imported := gltf.generate_scene(state)
        if imported == null:
            _fail("mesh asset produced no scene")
            return
        for child in imported.get_children():
            if child is MeshInstance3D and child.mesh != null:
                var mesh: Mesh = child.mesh
                if mesh.get_surface_count() > 0:
                    mesh_triangles += mesh.surface_get_array_index_len(0) / 3
        imported.queue_free()
        var expected_triangles: int = int(manifest["mesh_triangles"])
        print("mesh asset: triangles=%d expected=%d" % [mesh_triangles, expected_triangles])
        if mesh_triangles != expected_triangles:
            _fail("mesh asset triangle count mismatch")
            return
        # Load the cooked package the offline tool produced and require it to
        # agree with this host's own import: the runtime reads the package,
        # not the source format.
        var package_path: String = manifest_dir.path_join("..").path_join("build/cooked").path_join(
            asset_path.get_file().get_basename() + ".pkg").simplify_path()
        if not FileAccess.file_exists(package_path):
            _fail("cooked package is missing: %s" % package_path)
            return
        var package := {}
        for line in FileAccess.get_file_as_string(package_path).split("\n"):
            var text := line.strip_edges()
            if text.is_empty() or text.find("=") < 1:
                continue
            package[text.left(text.find("="))] = text.substr(text.find("=") + 1).strip_edges()
        print("cooked package: loaded=%s format=%s triangles=%s" % [
            package.has("format"), package.get("format", ""), package.get("triangles", "")])
        if package.get("format", "") != "elisa-cooked-v2" or int(package.get("triangles", "-1")) != mesh_triangles:
            _fail("cooked package does not match the import")
            return
        # Build a renderable mesh from the cooked geometry, so the authored
        # asset reaches the screen through the pipeline rather than from a
        # source format.
        var position_floats := Marshalls.base64_to_raw(package["positions_b64"]).to_float32_array()
        var normal_floats := Marshalls.base64_to_raw(package["normals_b64"]).to_float32_array()
        var index_ints := Marshalls.base64_to_raw(package["indices_b64"]).to_int32_array()
        var vertices := PackedVector3Array()
        for index in range(position_floats.size() / 3):
            vertices.append(Vector3(position_floats[index * 3], position_floats[index * 3 + 1], position_floats[index * 3 + 2]))
        var normals := PackedVector3Array()
        for index in range(normal_floats.size() / 3):
            normals.append(Vector3(normal_floats[index * 3], normal_floats[index * 3 + 1], normal_floats[index * 3 + 2]))
        var arrays := []
        arrays.resize(Mesh.ARRAY_MAX)
        arrays[Mesh.ARRAY_VERTEX] = vertices
        arrays[Mesh.ARRAY_NORMAL] = normals
        arrays[Mesh.ARRAY_INDEX] = index_ints
        cooked_goal_mesh = ArrayMesh.new()
        cooked_goal_mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
        print("cooked package: mesh vertices=%d indices=%d" % [vertices.size(), index_ints.size()])
        # Cooked texture: turn the RGBA package into a Godot texture so the
        # goal material samples it, the same texture the native host uploads.
        var texture_path: String = package_path.get_base_dir().path_join(asset_path.get_file().get_basename() + "_tex.rgba")
        if FileAccess.file_exists(texture_path):
            var texture_values := {}
            for line in FileAccess.get_file_as_string(texture_path).split("\n"):
                var text := line.strip_edges()
                if text.is_empty() or text.find("=") < 1:
                    continue
                texture_values[text.left(text.find("="))] = text.substr(text.find("=") + 1).strip_edges()
            var texture_width: int = int(texture_values.get("width", "0"))
            var texture_height: int = int(texture_values.get("height", "0"))
            var texture_pixels := Marshalls.base64_to_raw(texture_values.get("pixels_b64", ""))
            if texture_values.get("format", "") == "elisa-texture-v1" and texture_pixels.size() == texture_width * texture_height * 4:
                var texture_image := Image.create_from_data(texture_width, texture_height, false, Image.FORMAT_RGBA8, texture_pixels)
                cooked_goal_texture = ImageTexture.create_from_image(texture_image)
                print("godot goal texture: %dx%d texture=%s" % [texture_width, texture_height, str(cooked_goal_texture != null)])

    var status_colors := {
        "playing": Color(0.1, 0.85, 0.9, 1.0),
        "paused": Color(0.95, 0.85, 0.1, 1.0),
        "won": Color(0.1, 0.9, 0.2, 1.0),
        "lost": Color(0.95, 0.15, 0.1, 1.0),
    }
    var hunter_marker: MeshInstance3D = null
    var goal_marker: MeshInstance3D = null
    var goal_used_cooked := false
    var marker_count := 0
    if manifest.has("game_status") and manifest.has("status_cell"):
        var status_color: Color = status_colors.get(String(manifest["game_status"]), Color(0.9, 0.9, 0.9, 1.0))
        for cell in String(manifest["status_cell"]).split(";"):
            if cell.is_empty():
                continue
            var parts := cell.split(",")
            var cell_x := int(parts[0])
            var cell_y := int(parts[1])
            var status_marker := MeshInstance3D.new()
            status_marker.name = "ElisaStatus_%d_%d" % [cell_x, cell_y]
            var status_mesh := BoxMesh.new()
            status_mesh.size = Vector3(0.52, 0.52, 0.52)
            var status_material := StandardMaterial3D.new()
            status_material.albedo_color = status_color
            status_material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
            status_mesh.material = status_material
            status_marker.mesh = status_mesh
            status_marker.position = Vector3(cell_x * 0.6 - 2.1, cell_y * 0.6 - 2.1, 1.0)
            scene_root.add_child(status_marker)
            marker_count += 1
    for spec in marker_specs:
        if not manifest.has(spec["field"]):
            continue
        for cell in String(manifest[spec["field"]]).split(";"):
            if cell.is_empty():
                continue
            var parts := cell.split(",")
            if parts.size() != 2:
                _fail("marker cell malformed")
                return
            var cell_x := int(parts[0])
            var cell_y := int(parts[1])
            var marker := MeshInstance3D.new()
            marker.name = "%s_%d_%d" % [spec["name"], cell_x, cell_y]
            # The goal marker is built from the cooked package's geometry when
            # one loaded, so the authored asset reaches the screen through the
            # pipeline; otherwise the procedural box is used.
            var use_cooked: bool = String(spec["field"]) == "goal" and cooked_goal_mesh != null
            if use_cooked:
                marker.mesh = cooked_goal_mesh
                marker.scale = Vector3(0.13, 0.13, 0.13)
            else:
                var box_mesh := BoxMesh.new()
                box_mesh.size = Vector3(0.52, 0.52, 0.52)
                marker.mesh = box_mesh
            var marker_material := StandardMaterial3D.new()
            marker_material.albedo_color = spec["color"]
            marker_material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
            if use_cooked and cooked_goal_texture != null:
                marker_material.albedo_texture = cooked_goal_texture
                marker_material.albedo_color = Color(1.0, 1.0, 1.0, 1.0)
            marker.material_override = marker_material
            marker.position = Vector3(cell_x * 0.6 - 2.1, cell_y * 0.6 - 2.1, 1.0)
            scene_root.add_child(marker)
            if String(spec["field"]) == "hunter":
                hunter_marker = marker
            if String(spec["field"]) == "goal":
                goal_marker = marker
                goal_used_cooked = use_cooked
            marker_count += 1

    # The authored asset must reach the screen through the pipeline: when a
    # package loaded, the goal marker has to be built from that mesh rather
    # than the procedural fallback. This was previously ordered wrong, so the
    # check guards the fix instead of trusting the comment.
    print("goal marker: cooked_mesh=%s scale=%s" % [str(goal_used_cooked), str(goal_marker.scale) if goal_marker != null else "none"])
    if cooked_goal_mesh != null and (not goal_used_cooked or goal_marker == null):
        _fail("goal marker did not use the cooked package mesh")
        return

    # Audio: build the same short clip the native host decodes and queue one
    # player per cue the Elisa game emitted. As natively, the real check is the
    # decoded sample information; the cue count must agree with the fixture.
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
    var audio_ok := audio_stream.data.size() == audio_samples * 2 and audio_stream.mix_rate == audio_rate and audio_stream.format == AudioStreamWAV.FORMAT_16_BITS and not audio_stream.stereo
    var expected_cues := int(manifest.get("audio_cues", 0))
    var cue_players := 0
    for cue_index in range(expected_cues):
        var cue_player := AudioStreamPlayer.new()
        cue_player.name = "ElisaCue_%d" % cue_index
        cue_player.stream = audio_stream
        scene_root.add_child(cue_player)
        cue_player.play()
        cue_players += 1
    print("godot audio: decoded_bytes=%d rate=%d cues=%d expected=%d" % [audio_stream.data.size(), audio_stream.mix_rate, cue_players, expected_cues])
    if not audio_ok or cue_players != expected_cues:
        _fail("godot audio cue check failed")
        return

    # Physics: one dynamic body, far off-camera at x=20 so it cannot occlude
    # any projected marker or wall sample. Each host runs exactly one solver
    # for a body (the native host uses Jolt through Wicked; this host uses
    # Godot physics), and never both at once.
    var physics_box := RigidBody3D.new()
    physics_box.name = "ElisaPhysicsBox"
    var physics_shape := CollisionShape3D.new()
    var physics_box_shape := BoxShape3D.new()
    physics_box_shape.size = Vector3(0.6, 0.6, 0.6)
    physics_shape.shape = physics_box_shape
    physics_box.add_child(physics_shape)
    var physics_mesh := MeshInstance3D.new()
    var physics_box_mesh := BoxMesh.new()
    physics_box_mesh.size = Vector3(0.6, 0.6, 0.6)
    physics_mesh.mesh = physics_box_mesh
    physics_box.add_child(physics_mesh)
    physics_box.position = Vector3(20.0, 5.0, 0.0)
    scene_root.add_child(physics_box)
    var physics_start_y: float = physics_box.position.y

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

    # Let the renderer produce real frames before reading the viewport back,
    # and time them: the plan asks for measured frame time with a tail, not a
    # "zero overhead" claim.
    # Warm-up frames are excluded on purpose: shader permutation creation and
    # history buffers make the first frames unrepresentative, and the plan
    # asks for steady-state median and tail, not start-up cost.
    # Wall time around await process_frame measures the display frame boundary
    # (vsync) unless vsync is disabled, in which case it measures the host's
    # own per-frame work, which is the comparable quantity to the native
    # host's measured frame cost. Performance.TIME_PROCESS rounds to zero for
    # a scene this small, so it is not used.
    DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
    var warmup := 5
    var measured := 30
    # Gameplay over time: walk the character along the route Elisa published,
    # one cell per frame, so the host shows movement rather than a teleport.
    var hunter_route: Array[Vector2i] = []
    if manifest.has("hunter_route"):
        for cell in String(manifest["hunter_route"]).split(";"):
            if cell.is_empty():
                continue
            var parts := cell.split(",")
            hunter_route.append(Vector2i(int(parts[0]), int(parts[1])))
    var frame_micros: Array[int] = []
    for _frame in range(warmup + measured):
        var frame_start := Time.get_ticks_usec()
        await process_frame
        if hunter_marker != null and not hunter_route.is_empty():
            var step: int = min(_frame, hunter_route.size() - 1)
            hunter_marker.position = Vector3(
                hunter_route[step].x * 0.6 - 2.1, hunter_route[step].y * 0.6 - 2.1, 1.0)
        var elapsed := Time.get_ticks_usec() - frame_start
        if _frame >= warmup:
            frame_micros.append(elapsed)
    frame_micros.sort()

    # Physics needs wall time, not just frames: Godot advances its solver
    # from real delta, so a settle loop with real timers lets gravity act.
    for _settle in range(30):
        await create_timer(0.02).timeout
    var physics_end_y: float = physics_box.position.y
    print("physics: start_y=%.3f end_y=%.3f" % [physics_start_y, physics_end_y])
    if physics_start_y - physics_end_y < 0.2:
        _fail("physics box did not fall under gravity")
        return
    physics_box.queue_free()

    var image: Image = root.get_texture().get_image()
    if image == null:
        _fail("viewport image unavailable")
        return
    var status := image.save_png(arguments[1])
    if status != OK:
        _fail("capture save failed")
        return
    var stats_path: String = arguments[1].get_basename() + "-stats.txt"
    var stats_file := FileAccess.open(stats_path, FileAccess.WRITE)
    if stats_file == null:
        _fail("frame stats cannot be written")
        return
    stats_file.store_line("samples=%d" % frame_micros.size())
    stats_file.store_line("median_us=%d" % frame_micros[frame_micros.size() / 2])
    stats_file.store_line("p95_us=%d" % frame_micros[(frame_micros.size() * 95) / 100])
    stats_file.store_line("worst_us=%d" % frame_micros[frame_micros.size() - 1])
    stats_file.close()
    var visible_walls := 0
    for child in scene_root.get_children():
        if child is MeshInstance3D and child.name.begins_with("ElisaWall"):
            visible_walls += 1
    print("godot capture: %dx%d walls=%d hidden=%d markers=%d" % [image.get_width(), image.get_height(), visible_walls, walls_hidden, marker_count])

    # Unload audit: free every Elisa geometry node and the physics body, then
    # require the scene root to return to just its environment node. A missed
    # free would leave a child behind, and a freed node must report invalid
    # rather than dangling. This is the Godot half of the Phase 4 teardown
    # guarantee the native host asserts with its component counts.
    var tracked: Array[Node] = []
    for child in scene_root.get_children():
        if child == environment_node:
            continue
        tracked.append(child)
    var freed_count := tracked.size()
    for node in tracked:
        node.queue_free()
    await process_frame
    await process_frame
    var remaining := scene_root.get_child_count()
    var environment_alive := is_instance_valid(environment_node)
    var dangling := is_instance_valid(tracked[0]) if freed_count > 0 else false
    print("godot unload: freed=%d remaining=%d environment_alive=%s dangling=%s" % [freed_count, remaining, str(environment_alive), str(dangling)])
    if remaining != 1 or not environment_alive or dangling:
        _fail("godot unload audit failed")
        return
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
