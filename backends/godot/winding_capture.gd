extends SceneTree

# Rendered winding check for the Godot host, the sibling of probe.gd's
# headless facing check. Each triangle of the manifest's cooked package goes
# through cooked_mesh.gd and is drawn alone, back faces culled, white on black,
# from the side glTF calls its front (counter-clockwise in cooked order) and
# from behind. It must show from the front only. A closed mesh cannot prove
# this: with its front faces culled, the far side's inner faces fill the same
# silhouette. Needs a display driver; --headless renders nothing.

const CookedMesh = preload("res://cooked_mesh.gd")

func _initialize() -> void:
    call_deferred("_run_capture")

func _run_capture() -> void:
    var arguments := OS.get_cmdline_user_args()
    if arguments.size() != 1:
        _fail("expected <scene-manifest>")
        return
    var manifest := _key_values(arguments[0])
    if not manifest.has("mesh_asset"):
        _fail("scene manifest names no mesh asset")
        return
    var package_path: String = arguments[0].get_base_dir().path_join("..").path_join("build/cooked").path_join(
        String(manifest["mesh_asset"]).get_file().get_basename() + ".pkg").simplify_path()
    var package := _key_values(package_path)
    if package.get("format", "") != "elisa-cooked-v2":
        _fail("cooked package is missing: %s" % package_path)
        return
    var arrays := CookedMesh.surface_arrays(package)
    var positions: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
    var uploaded: PackedInt32Array = arrays[Mesh.ARRAY_INDEX]
    var cooked := Marshalls.base64_to_raw(package["indices_b64"]).to_int32_array()

    var environment := Environment.new()
    environment.background_mode = Environment.BG_COLOR
    environment.background_color = Color(0.0, 0.0, 0.0, 1.0)
    environment.ambient_light_source = Environment.AMBIENT_SOURCE_DISABLED
    var environment_node := WorldEnvironment.new()
    environment_node.environment = environment
    root.add_child(environment_node)
    var material := StandardMaterial3D.new()
    material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
    material.cull_mode = BaseMaterial3D.CULL_BACK
    var triangle := MeshInstance3D.new()
    triangle.material_override = material
    root.add_child(triangle)
    var camera := Camera3D.new()
    root.add_child(camera)
    camera.current = true

    var front_only := 0
    for first in range(0, cooked.size() - 2, 3):
        var a := positions[cooked[first]]
        var b := positions[cooked[first + 1]]
        var c := positions[cooked[first + 2]]
        var one := []
        one.resize(Mesh.ARRAY_MAX)
        one[Mesh.ARRAY_VERTEX] = PackedVector3Array([
            positions[uploaded[first]], positions[uploaded[first + 1]], positions[uploaded[first + 2]]])
        var mesh := ArrayMesh.new()
        mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, one)
        triangle.mesh = mesh
        var front := (b - a).cross(c - a).normalized()
        var centre := (a + b + c) / 3.0
        var distance := maxf(maxf((b - a).length(), (c - b).length()), (a - c).length()) * 1.5
        var from_front: bool = await _centre_drawn(camera, centre + front * distance, centre)
        var from_behind: bool = await _centre_drawn(camera, centre - front * distance, centre)
        if from_front and not from_behind:
            front_only += 1
        else:
            print("godot winding capture: triangle %d front=%s behind=%s" % [first / 3, from_front, from_behind])
    print("godot winding capture: renderer=%s triangles=%d front_only=%d" % [
        RenderingServer.get_current_rendering_method(), cooked.size() / 3, front_only])
    if cooked.size() < 3 or front_only != cooked.size() / 3:
        _fail("cooked triangles are not drawn from their glTF front only")
        return
    quit(0)

func _centre_drawn(camera: Camera3D, eye: Vector3, target: Vector3) -> bool:
    camera.position = eye
    camera.look_at(target, Vector3.UP if absf((target - eye).normalized().y) < 0.9 else Vector3.RIGHT)
    for _frame in range(3):
        await process_frame
    var image: Image = root.get_texture().get_image()
    return image.get_pixel(image.get_width() / 2, image.get_height() / 2).r > 0.5

func _key_values(filename: String) -> Dictionary:
    var values := {}
    if not FileAccess.file_exists(filename):
        return values
    for line in FileAccess.get_file_as_string(filename).split("\n"):
        var text := line.strip_edges()
        if text.is_empty() or text.begins_with("#") or text.find("=") < 1:
            continue
        values[text.left(text.find("="))] = text.substr(text.find("=") + 1).strip_edges()
    return values

func _fail(message: String) -> void:
    push_error(message)
    quit(1)
