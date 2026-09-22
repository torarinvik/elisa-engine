extends RefCounted

# The Godot upload boundary for cooked geometry, shared by probe.gd and
# capture.gd. Godot is right-handed with +Y up, like Elisa, so positions and
# normals cross unchanged. Winding does not: cooked packages keep glTF's front
# faces, counter-clockwise seen from the side they face, and Godot draws
# clockwise ones (its own glTF importer swaps the same two indices). This is
# the one place a cooked triangle's winding is reversed. See
# docs/validation/godot-cooked-winding.md.

static func godot_indices(cooked: PackedInt32Array) -> PackedInt32Array:
    var indices := cooked.duplicate()
    for first in range(0, indices.size() - 2, 3):
        var second := indices[first + 1]
        indices[first + 1] = indices[first + 2]
        indices[first + 2] = second
    return indices

static func surface_arrays(package: Dictionary) -> Array:
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
    arrays[Mesh.ARRAY_INDEX] = godot_indices(index_ints)
    return arrays
