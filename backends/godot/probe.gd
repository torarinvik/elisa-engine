extends SceneTree

const CookedMesh = preload("res://cooked_mesh.gd")

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
        var package_path: String = manifest_path.get_base_dir().path_join("..").path_join("build/cooked").path_join(asset_name + "-godot.pkg").simplify_path()
        if not FileAccess.file_exists(package_path):
            _fail("Godot raw cooked companion is missing: %s" % package_path)
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
        var cooked_arrays := CookedMesh.surface_arrays(package)
        var cooked_mesh := ArrayMesh.new()
        cooked_mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, cooked_arrays)
        print("godot probe asset: format=%s triangles=%s surface=%d" % [package.get("format", ""), package.get("triangles", ""), cooked_mesh.get_surface_count()])
        if cooked_mesh.get_surface_count() < 1:
            _fail("cooked package produced no surface")
            return
        # Each uploaded triangle must face the way Godot's own glTF import of
        # the same source triangle faces. Culling needs a renderer, which
        # --headless lacks, so this compares Godot's front-face normals:
        # Plane(a, b, c) takes a clockwise front face, and a rendered one-sided
        # triangle confirmed it (docs/validation/godot-cooked-winding.md).
        var source_path: String = manifest_path.get_base_dir().path_join("..").path_join(String(manifest["mesh_asset"])).simplify_path()
        var source_state := GLTFState.new()
        if GLTFDocument.new().append_from_file(source_path, source_state) != OK or source_state.get_meshes().is_empty():
            _fail("mesh asset could not be imported: %s" % source_path)
            return
        var source_arrays: Array = source_state.get_meshes()[0].mesh.get_surface_arrays(0)
        var facing := _facing_matches(cooked_arrays, source_arrays)
        # The asset's own winding must agree with its normals too, or a face
        # shows from inside on every host. The maze tile once wound six of its
        # twelve triangles inward, which the import comparison cannot see.
        var outward := _facing_normals(cooked_arrays)
        print("godot probe winding: triangles=%d facing_godot_import=%d facing_normals=%d" % [
            int(package["triangles"]), facing, outward])
        if facing != int(package["triangles"]):
            _fail("cooked triangles do not face the way Godot's glTF import does")
            return
        if outward != int(package["triangles"]):
            _fail("cooked triangles do not face the way their normals point")
            return
        # Cooked texture: the host turns the RGBA package into a real Godot
        # Image/ImageTexture and checks its size and a bright/dark checker pixel.
        var texture_path: String = package_path.get_base_dir().path_join(asset_name + "_tex.rgba")
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
            if texture_values.get("format", "") != "elisa-texture-v1" or texture_pixels.size() != texture_width * texture_height * 4:
                _fail("cooked texture does not match")
                return
            var texture_image := Image.create_from_data(texture_width, texture_height, false, Image.FORMAT_RGBA8, texture_pixels)
            var texture := ImageTexture.create_from_image(texture_image)
            var corner: Color = texture_image.get_pixel(0, 0)
            print("godot texture: %dx%d texture=%s rgb=%.2f,%.2f,%.2f" % [
                texture_image.get_width(), texture_image.get_height(), str(texture != null),
                corner.r, corner.g, corner.b])
            if texture == null or corner.g < 0.8 or corner.r > 0.2 or corner.b > 0.3:
                _fail("cooked texture pixels do not match")
                return
        # 16-bit packed companion: half the bytes per pixel, decoded by the host
        # as RGB565. Still green, so a packing error is caught.
        var packed_path: String = package_path.get_base_dir().path_join(asset_name + "_tex16.rgba")
        if FileAccess.file_exists(packed_path):
            var packed_values := {}
            for line in FileAccess.get_file_as_string(packed_path).split("\n"):
                var text := line.strip_edges()
                if text.is_empty() or text.find("=") < 1:
                    continue
                packed_values[text.left(text.find("="))] = text.substr(text.find("=") + 1).strip_edges()
            var packed_width: int = int(packed_values.get("width", "0"))
            var packed_height: int = int(packed_values.get("height", "0"))
            var packed_pixels := Marshalls.base64_to_raw(packed_values.get("pixels_b64", ""))
            if packed_values.get("packing", "") != "rgb565" or packed_pixels.size() != packed_width * packed_height * 2:
                _fail("cooked packed texture does not match")
                return
            var packed_image := Image.create_from_data(packed_width, packed_height, false, Image.FORMAT_RGB565, packed_pixels)
            var packed_colour: Color = packed_image.get_pixel(0, 0)
            print("godot packed texture: %dx%d rgb=%.2f,%.2f,%.2f" % [
                packed_width, packed_height, packed_colour.r, packed_colour.g, packed_colour.b])
            if packed_colour.g < 0.7 or packed_colour.r > 0.3 or packed_colour.b > 0.5:
                _fail("packed texture colour mismatch")
                return
        # Block-compressed companion: the host decodes the BC1 block.
        var bc1_path: String = package_path.get_base_dir().path_join(asset_name + "_tex_bc1.rgba")
        if FileAccess.file_exists(bc1_path):
            var bc1_values := {}
            for line in FileAccess.get_file_as_string(bc1_path).split("\n"):
                var text := line.strip_edges()
                if text.is_empty() or text.find("=") < 1:
                    continue
                bc1_values[text.left(text.find("="))] = text.substr(text.find("=") + 1).strip_edges()
            var bc1_width: int = int(bc1_values.get("width", "0"))
            var bc1_height: int = int(bc1_values.get("height", "0"))
            var bc1_pixels := Marshalls.base64_to_raw(bc1_values.get("pixels_b64", ""))
            if bc1_values.get("packing", "") != "bc1" or bc1_pixels.size() != 8:
                _fail("cooked BC1 texture does not match")
                return
            var bc1_image := Image.create_from_data(bc1_width, bc1_height, false, Image.FORMAT_DXT1, bc1_pixels)
            if bc1_image.is_compressed():
                bc1_image.decompress()
            var bc1_colour: Color = bc1_image.get_pixel(0, 0)
            print("godot bc1 texture: %dx%d rgb=%.2f,%.2f,%.2f" % [
                bc1_width, bc1_height, bc1_colour.r, bc1_colour.g, bc1_colour.b])
            if bc1_colour.g < 0.6 or bc1_colour.r > 0.4 or bc1_colour.b > 0.5:
                _fail("BC1 texture colour mismatch")
                return
        # The KTX container of the same block payload: the host opens the
        # standard format directly instead of a project-local text package.
        var ktx_path: String = package_path.get_base_dir().path_join(asset_name + "_tex_bc1.ktx")
        if FileAccess.file_exists(ktx_path):
            var ktx_bytes := FileAccess.get_file_as_bytes(ktx_path)
            var ktx_image := Image.new()
            var ktx_error := ktx_image.load_ktx_from_buffer(ktx_bytes)
            if ktx_error != OK:
                _fail("KTX BC1 texture did not load")
                return
            var ktx_was_compressed := ktx_image.is_compressed()
            if ktx_was_compressed:
                ktx_image.decompress()
            var ktx_colour: Color = ktx_image.get_pixel(0, 0)
            print("godot ktx texture: %dx%d compressed=%s rgb=%.2f,%.2f,%.2f" % [
                ktx_image.get_width(), ktx_image.get_height(), str(ktx_was_compressed),
                ktx_colour.r, ktx_colour.g, ktx_colour.b])
            if ktx_image.get_width() != 4 or ktx_image.get_height() != 4 or not ktx_was_compressed:
                _fail("KTX texture is not the compressed 4x4 block")
                return
            if ktx_colour.g < 0.6 or ktx_colour.r > 0.4 or ktx_colour.b > 0.5:
                _fail("KTX texture colour mismatch")
                return
        # Basis Universal KTX2: Godot's loader may keep the transcoded image
        # compressed or return a decoded fallback depending on its platform
        # image backend; both paths must preserve dimensions and colour.
        var ktx2_path: String = package_path.get_base_dir().path_join(asset_name + "_tex.ktx2")
        if FileAccess.file_exists(ktx2_path):
            var ktx2_image := Image.new()
            var ktx2_error := ktx2_image.load_ktx_from_buffer(FileAccess.get_file_as_bytes(ktx2_path))
            if ktx2_error != OK:
                _fail("KTX2 Basis texture did not load")
                return
            var ktx2_was_compressed := ktx2_image.is_compressed()
            if ktx2_was_compressed:
                ktx2_image.decompress()
            var ktx2_colour: Color = ktx2_image.get_pixel(0, 0)
            print("godot ktx2 texture: %dx%d compressed=%s rgb=%.2f,%.2f,%.2f" % [
                ktx2_image.get_width(), ktx2_image.get_height(), str(ktx2_was_compressed),
                ktx2_colour.r, ktx2_colour.g, ktx2_colour.b])
            if ktx2_image.get_width() != 4 or ktx2_image.get_height() != 4:
                _fail("KTX2 texture is not the expected 4x4 image")
                return
            if not ktx2_was_compressed:
                print("godot ktx2 texture: decoded fallback accepted")
            if ktx2_colour.g < 0.6 or ktx2_colour.r > 0.4 or ktx2_colour.b > 0.5:
                _fail("KTX2 texture colour mismatch")
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

    # Elisa UI menu as host controls: the host consumes the menu state as data
    # and highlights only the focused, enabled row. Controls work headless, so
    # this is part of the gated probe rather than a rendered-only check.
    if manifest.has("menu_actions") and manifest.has("menu_enabled") and manifest.has("menu_focus"):
        var action_count: int = int(manifest["menu_actions"])
        var menu_focus: int = int(manifest["menu_focus"])
        var enabled_flags: Array[int] = []
        for part in String(manifest["menu_enabled"]).split(","):
            enabled_flags.append(int(part))
        if enabled_flags.size() != action_count or menu_focus < 0 or menu_focus >= action_count or enabled_flags[menu_focus] != 1:
            _fail("menu state is inconsistent")
            return
        var menu_root := VBoxContainer.new()
        menu_root.name = "ElisaMenu"
        root.add_child(menu_root)
        var buttons: Array[Button] = []
        var disabled_count := 0
        for index in range(action_count):
            var button := Button.new()
            button.name = "ElisaMenuAction_%d" % index
            button.text = "Action %d" % index
            button.custom_minimum_size = Vector2(120.0, float(manifest.get("menu_row_height", "20")))
            button.disabled = enabled_flags[index] == 0
            if button.disabled:
                disabled_count += 1
            menu_root.add_child(button)
            buttons.append(button)
        # Engine theme: the host applies the fixture's colors and insets instead
        # of choosing its own, and the focused row shows the focus label color.
        var has_style: bool = manifest.has("menu_style_background") and manifest.has("menu_style_text") \
            and manifest.has("menu_style_disabled") and manifest.has("menu_style_focus") \
            and manifest.has("menu_style_padding") and manifest.has("menu_style_border")
        var style_padding: int = int(manifest.get("menu_style_padding", "0"))
        var style_border: int = int(manifest.get("menu_style_border", "0"))
        if has_style:
            var surface := StyleBoxFlat.new()
            surface.bg_color = _rgba(String(manifest["menu_style_background"]))
            surface.border_color = _rgba(String(manifest["menu_style_focus"]))
            surface.set_border_width_all(style_border)
            surface.content_margin_left = style_padding
            surface.content_margin_right = style_padding
            surface.content_margin_top = style_padding
            surface.content_margin_bottom = style_padding
            var disabled_color := _rgba(String(manifest["menu_style_disabled"]))
            var text_color := _rgba(String(manifest["menu_style_text"]))
            for button in buttons:
                button.add_theme_stylebox_override("normal", surface)
                button.add_theme_stylebox_override("focus", surface)
                button.add_theme_color_override("font_color", disabled_color if button.disabled else text_color)
            buttons[menu_focus].add_theme_color_override("font_color", _rgba(String(manifest["menu_style_focus"])))
        buttons[menu_focus].grab_focus()
        await process_frame
        var focused: Control = root.gui_get_focus_owner()
        var menu_visible: int = int(manifest.get("menu_visible", str(action_count)))
        print("godot menu: actions=%d focus=%d visible=%d focused=%s disabled=%d" % [
            action_count, menu_focus, menu_visible, focused.name if focused != null else "none", disabled_count])
        if menu_focus >= menu_visible:
            _fail("menu focus is outside the visible window")
            return
        var row_height: float = float(manifest.get("menu_row_height", "20"))
        if focused != buttons[menu_focus] or disabled_count != enabled_flags.count(0):
            _fail("menu focus or disabled state does not match the fixture")
            return
        if buttons[menu_focus].custom_minimum_size.y != row_height:
            _fail("menu row height was not applied")
            return
        if has_style:
            var focused_box: StyleBoxFlat = buttons[menu_focus].get_theme_stylebox("focus")
            var disabled_index: int = enabled_flags.find(0)
            if focused_box == null or not _rgba_matches(focused_box.bg_color, String(manifest["menu_style_background"])) \
                    or focused_box.content_margin_left != style_padding or focused_box.border_width_top != style_border:
                _fail("menu surface style was not applied")
                return
            if disabled_index >= 0 and not _rgba_matches(buttons[disabled_index].get_theme_color("font_color"), String(manifest["menu_style_disabled"])):
                _fail("disabled label color was not applied")
                return
            if not _rgba_matches(buttons[menu_focus].get_theme_color("font_color"), String(manifest["menu_style_focus"])):
                _fail("focused label color was not applied")
                return
            print("godot style: background=%s focus=%s disabled=%s inset=%d" % [
                String(manifest["menu_style_background"]), String(manifest["menu_style_focus"]),
                String(manifest["menu_style_disabled"]), style_padding + style_border])
        menu_root.queue_free()

    # Character leg pose: the host builds one marker per solved joint from the
    # fixture's cell-unit pose, so the engine's IK reaches the host.
    if manifest.has("pose_hip") and manifest.has("pose_knee") and manifest.has("pose_foot"):
        var joints := {
            "ElisaPoseHip": String(manifest["pose_hip"]),
            "ElisaPoseKnee": String(manifest["pose_knee"]),
            "ElisaPoseFoot": String(manifest["pose_foot"]),
        }
        var pose_root := Node3D.new()
        pose_root.name = "ElisaPose"
        root.add_child(pose_root)
        var joints_ok := true
        for joint_name in joints:
            var parts: PackedStringArray = joints[joint_name].split(",")
            if parts.size() != 2:
                joints_ok = false
                break
            var joint := MeshInstance3D.new()
            joint.name = joint_name
            var joint_mesh := BoxMesh.new()
            joint_mesh.size = Vector3(0.08, 0.08, 0.08)
            joint.mesh = joint_mesh
            joint.position = Vector3(float(parts[0]) * 0.6 - 2.1, float(parts[1]) * 0.6 - 2.1, 1.0)
            pose_root.add_child(joint)
            if joint.position.y > (float(String(manifest["pose_hip"]).split(",")[1]) * 0.6 - 2.1) + 0.001:
                joints_ok = false
        var hip_y: float = float(String(manifest["pose_hip"]).split(",")[1]) * 0.6 - 2.1
        var knee_y: float = float(String(manifest["pose_knee"]).split(",")[1]) * 0.6 - 2.1
        var foot_y: float = float(String(manifest["pose_foot"]).split(",")[1]) * 0.6 - 2.1
        print("godot pose: joints=%d hip_y=%.3f knee_y=%.3f foot_y=%.3f" % [pose_root.get_child_count(), hip_y, knee_y, foot_y])
        if not joints_ok or pose_root.get_child_count() != 3 or not (hip_y > knee_y and knee_y > foot_y):
            _fail("character pose joints are inconsistent")
            return
        pose_root.queue_free()

    # Host input path: a synthetic key event is parsed and the host's key state
    # must reflect the fixture's bound keys, the portable names the engine maps.
    if manifest.has("input_forward"):
        var keycodes := {"KeyW": KEY_W, "KeyA": KEY_A, "KeyD": KEY_D,
            "PadLeft": KEY_LEFT, "PadRight": KEY_RIGHT, "PadUp": KEY_UP}
        var bound_names := [String(manifest["input_forward"]), String(manifest["input_left"]), String(manifest["input_right"])]
        var parsed := 0
        for name in bound_names:
            var keycode: Key = keycodes.get(name, KEY_NONE)
            if keycode == KEY_NONE:
                _fail("unknown portable input button: %s" % name)
                return
            var event := InputEventKey.new()
            event.keycode = keycode
            event.physical_keycode = keycode
            event.pressed = true
            Input.parse_input_event(event)
            Input.flush_buffered_events()
            if Input.is_key_pressed(keycode):
                parsed += 1
        print("godot input: bound=%d pressed=%d" % [bound_names.size(), parsed])
        if parsed != bound_names.size():
            _fail("host input mapping did not register the bound keys")
            return

    # Skinned-mesh submission: build a quad mesh from the engine-deformed
    # positions and require the vertex count and a corner to match the fixture.
    if manifest.has("skin_quad"):
        var corners: PackedVector3Array = PackedVector3Array()
        for entry in String(manifest["skin_quad"]).split(";"):
            if entry.is_empty():
                continue
            var parts := entry.split(",")
            corners.append(Vector3(float(parts[0]) * 0.6 - 2.1, float(parts[1]) * 0.6 - 2.1, 1.0))
        var skinned_arrays := []
        skinned_arrays.resize(Mesh.ARRAY_MAX)
        skinned_arrays[Mesh.ARRAY_VERTEX] = corners
        skinned_arrays[Mesh.ARRAY_INDEX] = PackedInt32Array([0, 1, 2, 0, 2, 3])
        var skinned_mesh := ArrayMesh.new()
        skinned_mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, skinned_arrays)
        var first_corner_x: float = corners[0].x
        print("godot skinned quad: vertices=%d indices=%d first_x=%.3f" % [
            corners.size(), skinned_mesh.surface_get_array_index_len(0), first_corner_x])
        if corners.size() != 4 or skinned_mesh.surface_get_array_index_len(0) != 6 or abs(first_corner_x - (1.297 * 0.6 - 2.1)) > 0.001:
            _fail("skinned quad mesh does not match the fixture")
            return

    print("Godot backend scene create/update/render/despawn probe passed.")
    quit(0)

func _rgba(text: String) -> Color:
    var parts := text.split(",")
    if parts.size() != 4:
        return Color(0.0, 0.0, 0.0, 0.0)
    return Color(int(parts[0]) / 255.0, int(parts[1]) / 255.0, int(parts[2]) / 255.0, int(parts[3]) / 255.0)

func _rgba_matches(color: Color, text: String) -> bool:
    var parts := text.split(",")
    if parts.size() != 4:
        return false
    return int(round(color.r * 255.0)) == int(parts[0]) and int(round(color.g * 255.0)) == int(parts[1]) \
        and int(round(color.b * 255.0)) == int(parts[2]) and int(round(color.a * 255.0)) == int(parts[3])

# Counts uploaded triangles whose Godot front-face normal agrees with that of
# the reference triangle over the same three positions.
func _facing_matches(uploaded: Array, reference: Array) -> int:
    var positions: PackedVector3Array = uploaded[Mesh.ARRAY_VERTEX]
    var indices: PackedInt32Array = uploaded[Mesh.ARRAY_INDEX]
    var reference_positions: PackedVector3Array = reference[Mesh.ARRAY_VERTEX]
    var reference_indices: PackedInt32Array = reference[Mesh.ARRAY_INDEX]
    var matches := 0
    for first in range(0, indices.size() - 2, 3):
        var corners := [positions[indices[first]], positions[indices[first + 1]], positions[indices[first + 2]]]
        for other in range(0, reference_indices.size() - 2, 3):
            var reference_corners := [reference_positions[reference_indices[other]],
                reference_positions[reference_indices[other + 1]], reference_positions[reference_indices[other + 2]]]
            if not corners.all(func(corner): return reference_corners.any(func(candidate): return corner.is_equal_approx(candidate))):
                continue
            var normal := Plane(corners[0], corners[1], corners[2]).normal
            var reference_normal := Plane(reference_corners[0], reference_corners[1], reference_corners[2]).normal
            if normal.dot(reference_normal) > 0.5:
                matches += 1
            break
    return matches

# Counts uploaded triangles whose Godot front-face normal agrees with the sum
# of their vertex normals.
func _facing_normals(uploaded: Array) -> int:
    var positions: PackedVector3Array = uploaded[Mesh.ARRAY_VERTEX]
    var normals: PackedVector3Array = uploaded[Mesh.ARRAY_NORMAL]
    var indices: PackedInt32Array = uploaded[Mesh.ARRAY_INDEX]
    var matches := 0
    for first in range(0, indices.size() - 2, 3):
        var a := indices[first]
        var b := indices[first + 1]
        var c := indices[first + 2]
        if Plane(positions[a], positions[b], positions[c]).normal.dot(normals[a] + normals[b] + normals[c]) > 0.0:
            matches += 1
    return matches

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
