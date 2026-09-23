"""Normalize the bounded skin portion of a glTF runtime mesh package.

The runtime package stores four influences per vertex and a parent-ordered rig.
Authored inverse-bind matrices stay in the source skin palette order; when the
optional glTF accessor is absent, the spec's identity matrices are made explicit.
"""

from __future__ import annotations

import math
import struct

import cook_assets
import cook_gltf_animation
import cook_gltf_nodes

MAX_JOINTS = 64
MAX_RIG_NODES = cook_gltf_nodes.MAX_NODES
GLTF_IDENTITY_MATRIX = (
    1.0, 0.0, 0.0, 0.0,
    0.0, 1.0, 0.0, 0.0,
    0.0, 0.0, 1.0, 0.0,
    0.0, 0.0, 0.0, 1.0,
)


def _accessor(document: dict, index: int, type_name: str, label: str) -> dict:
    accessors = document.get("accessors", [])
    if type(index) is not int or not 0 <= index < len(accessors):
        raise ValueError(f"{label} accessor index is out of range")
    value = accessors[index]
    if not isinstance(value, dict) or value.get("type") != type_name:
        raise ValueError(f"{label} accessor must be {type_name}")
    return value


def _rest_matrix_transform(matrix: tuple[float, ...], label: str) -> tuple[float, ...]:
    columns = [(matrix[0], matrix[4], matrix[8]),
        (matrix[1], matrix[5], matrix[9]), (matrix[2], matrix[6], matrix[10])]
    scales = [math.sqrt(sum(value * value for value in column)) for column in columns]
    if any(not math.isfinite(scale) or scale <= 1.0e-12 for scale in scales):
        raise ValueError(f"{label} matrix must have nonzero scale")
    if cook_gltf_nodes.determinant(matrix) < 0.0:
        scales[0] = -scales[0]
    rotation = [[matrix[row * 4 + column] / scales[column] for column in range(3)]
        for row in range(3)]
    orthogonality = (sum(rotation[row][0] * rotation[row][1] for row in range(3)),
        sum(rotation[row][0] * rotation[row][2] for row in range(3)),
        sum(rotation[row][1] * rotation[row][2] for row in range(3)))
    if any(abs(value) > 1.0e-5 for value in orthogonality):
        raise ValueError(f"{label} matrix contains shear")
    trace = rotation[0][0] + rotation[1][1] + rotation[2][2]
    if trace > 0.0:
        factor = math.sqrt(trace + 1.0) * 2.0
        quaternion = ((rotation[2][1] - rotation[1][2]) / factor,
            (rotation[0][2] - rotation[2][0]) / factor,
            (rotation[1][0] - rotation[0][1]) / factor, factor * 0.25)
    elif rotation[0][0] > rotation[1][1] and rotation[0][0] > rotation[2][2]:
        factor = math.sqrt(1.0 + rotation[0][0] - rotation[1][1] - rotation[2][2]) * 2.0
        quaternion = (factor * 0.25, (rotation[0][1] + rotation[1][0]) / factor,
            (rotation[0][2] + rotation[2][0]) / factor, (rotation[2][1] - rotation[1][2]) / factor)
    elif rotation[1][1] > rotation[2][2]:
        factor = math.sqrt(1.0 + rotation[1][1] - rotation[0][0] - rotation[2][2]) * 2.0
        quaternion = ((rotation[0][1] + rotation[1][0]) / factor, factor * 0.25,
            (rotation[1][2] + rotation[2][1]) / factor, (rotation[0][2] - rotation[2][0]) / factor)
    else:
        factor = math.sqrt(1.0 + rotation[2][2] - rotation[0][0] - rotation[1][1]) * 2.0
        quaternion = ((rotation[0][2] + rotation[2][0]) / factor,
            (rotation[1][2] + rotation[2][1]) / factor, factor * 0.25,
            (rotation[1][0] - rotation[0][1]) / factor)
    quaternion_length = math.sqrt(sum(value * value for value in quaternion))
    if not math.isfinite(quaternion_length) or quaternion_length <= 1.0e-12:
        raise ValueError(f"{label} matrix has an invalid rotation")
    quaternion = tuple(value / quaternion_length for value in quaternion)
    return (matrix[3], matrix[7], matrix[11], *quaternion, *scales)


def _rest_transform(node: dict, label: str) -> tuple[float, ...]:
    if "matrix" in node:
        return _rest_matrix_transform(cook_gltf_nodes.local_matrix(node), label)
    translation = cook_gltf_nodes.finite_numbers(node.get("translation", [0.0, 0.0, 0.0]), 3, label)
    rotation = cook_gltf_nodes.finite_numbers(node.get("rotation", [0.0, 0.0, 0.0, 1.0]), 4, label)
    length = math.sqrt(sum(value * value for value in rotation))
    if not math.isfinite(length) or length <= 1.0e-12 or abs(length - 1.0) > cook_gltf_nodes.ROTATION_TOLERANCE:
        raise ValueError(f"{label} rotation must be a unit quaternion")
    scale = cook_gltf_nodes.finite_numbers(node.get("scale", [1.0, 1.0, 1.0]), 3, label)
    if any(value == 0.0 for value in scale):
        raise ValueError(f"{label} scale must be nonzero")
    return (*translation, *(value / length for value in rotation), *scale)


def _inverse_affine(matrix: tuple[float, ...], label: str) -> tuple[float, ...]:
    a, b, c, tx, d, e, f, ty, g, h, i, tz = matrix
    determinant = cook_gltf_nodes.determinant(matrix)
    if not math.isfinite(determinant) or determinant == 0.0:
        raise ValueError(f"{label} must be invertible for skin hierarchy conversion")
    inverse = (
        (e * i - f * h) / determinant, (c * h - b * i) / determinant, (b * f - c * e) / determinant,
        (f * g - d * i) / determinant, (a * i - c * g) / determinant, (c * d - a * f) / determinant,
        (d * h - e * g) / determinant, (b * g - a * h) / determinant, (a * e - b * d) / determinant,
    )
    return (inverse[0], inverse[1], inverse[2], -(inverse[0] * tx + inverse[1] * ty + inverse[2] * tz),
        inverse[3], inverse[4], inverse[5], -(inverse[3] * tx + inverse[4] * ty + inverse[5] * tz),
        inverse[6], inverse[7], inverse[8], -(inverse[6] * tx + inverse[7] * ty + inverse[8] * tz))


def _read_inverse_bind(document: dict, buffer: bytes, skin: dict, joint_count: int) -> list[float]:
    reference = skin.get("inverseBindMatrices")
    if reference is None:
        return list(GLTF_IDENTITY_MATRIX) * joint_count
    accessor = _accessor(document, reference, "MAT4", "inverseBindMatrices")
    accessor_count = accessor.get("count")
    if (accessor.get("componentType") != 5126 or type(accessor_count) is not int or
            accessor_count < joint_count):
        raise ValueError("inverseBindMatrices must contain at least one float32 MAT4 per joint")
    data = cook_assets.accessor_bytes(document, buffer, reference)
    if len(data) < joint_count * 64:
        raise ValueError("inverseBindMatrices byte length does not match its joint count")
    matrices = [struct.unpack_from("<16f", data, offset)
        for offset in range(0, joint_count * 64, 64)]
    for matrix in matrices:
        if not all(math.isfinite(value) for value in matrix):
            raise ValueError("inverseBindMatrices contain non-finite values")
        # glTF node transforms and inverse bind transforms are affine. Reject
        # perspective rows and singular linear transforms rather than silently
        # producing a broken armature palette.
        if any(abs(matrix[index]) > 1.0e-6 for index in (3, 7, 11)) or abs(matrix[15] - 1.0) > 1.0e-6:
            raise ValueError("inverseBindMatrices must contain affine transforms")
        a, b, c = matrix[0], matrix[4], matrix[8]
        d, e, f = matrix[1], matrix[5], matrix[9]
        g, h, i = matrix[2], matrix[6], matrix[10]
        determinant = (a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g))
        if not math.isfinite(determinant) or determinant == 0.0:
            raise ValueError("inverseBindMatrices must be invertible")
    return [value for matrix in matrices for value in matrix]


def read_influences(document: dict, buffer: bytes, attributes: dict,
        vertex_count: int) -> tuple[list[int], list[float]]:
    joint_reference = attributes.get("JOINTS_0")
    weight_reference = attributes.get("WEIGHTS_0")
    if joint_reference is None or weight_reference is None:
        raise ValueError("a skinned primitive needs JOINTS_0 and WEIGHTS_0")
    joint_accessor = _accessor(document, joint_reference, "VEC4", "JOINTS_0")
    weight_accessor = _accessor(document, weight_reference, "VEC4", "WEIGHTS_0")
    if joint_accessor.get("count") != vertex_count or weight_accessor.get("count") != vertex_count:
        raise ValueError("skin influence counts do not match POSITION")
    if joint_accessor.get("componentType") not in (5121, 5123) or joint_accessor.get("normalized", False):
        raise ValueError("JOINTS_0 must be an unnormalized uint8 or uint16 VEC4")
    if weight_accessor.get("componentType") != 5126:
        raise ValueError("WEIGHTS_0 must be a float32 VEC4")
    joint_data = cook_assets.accessor_bytes(document, buffer, joint_reference)
    weight_data = cook_assets.accessor_bytes(document, buffer, weight_reference)
    joint_format = "<4B" if joint_accessor["componentType"] == 5121 else "<4H"
    joints: list[int] = []
    weights: list[float] = []
    for vertex in range(vertex_count):
        joints.extend(struct.unpack_from(joint_format, joint_data,
            vertex * (8 if joint_format == "<4H" else 4)))
        values = struct.unpack_from("<4f", weight_data, vertex * 16)
        if any(not math.isfinite(value) or value < 0.0 for value in values) or abs(sum(values) - 1.0) > 0.005:
            raise ValueError("skin weights must be finite, nonnegative, and normalized")
        weights.extend(values)
    return joints, weights


def _normalize_single(document: dict, buffer: bytes) -> dict | None:
    """Normalize one glTF skin into a parent-ordered runtime rig."""
    skins = document.get("skins", [])
    nodes = document.get("nodes", [])
    if not isinstance(nodes, list):
        raise ValueError("glTF nodes must be a list")
    mesh_nodes = [node for node in nodes if isinstance(node, dict) and "mesh" in node]
    if any(isinstance(node, dict) and "skin" in node and "mesh" not in node for node in nodes):
        raise ValueError("only mesh nodes may reference a skin")
    has_skin_attributes = any(
        isinstance(primitive, dict) and isinstance(primitive.get("attributes"), dict) and
        {"JOINTS_0", "WEIGHTS_0"} & set(primitive["attributes"])
        for mesh in document.get("meshes", []) if isinstance(mesh, dict)
        for primitive in mesh.get("primitives", []) if isinstance(mesh.get("primitives", []), list)
    )
    if not skins and not has_skin_attributes:
        if document.get("animations"):
            raise ValueError("glTF animation clips require a skinned mesh")
        if any("skin" in node for node in mesh_nodes):
            raise ValueError("mesh node skin requires a declared skin and influence attributes")
        return None
    if not isinstance(skins, list) or len(skins) != 1 or not isinstance(skins[0], dict):
        raise ValueError("runtime glTF cooker supports exactly one skin")
    skin = skins[0]
    if set(skin) - {"name", "joints", "skeleton", "inverseBindMatrices"}:
        raise ValueError("runtime glTF cooker encountered unsupported skin properties")
    joints = skin.get("joints")
    if not isinstance(joints, list) or not 1 <= len(joints) <= MAX_JOINTS or len(set(joints)) != len(joints) or \
            any(type(value) is not int or value < 0 or value >= len(nodes) for value in joints):
        raise ValueError(f"runtime glTF skin requires 1 to {MAX_JOINTS} distinct joint nodes")
    joint_set = set(joints)
    for node in mesh_nodes:
        if type(node.get("skin")) is not int or node.get("skin") != 0:
            raise ValueError("every skinned mesh node must reference skin 0")
    if not mesh_nodes or any(node.get("skin") != 0 for node in mesh_nodes):
        raise ValueError("a skin must be referenced by a mesh node")
    skeleton = skin.get("skeleton")
    if skeleton is not None and (type(skeleton) is not int or skeleton not in joint_set):
        raise ValueError("skin skeleton must name one of its joints")

    source_parents = [None] * len(nodes)
    for parent_index, node in enumerate(nodes):
        if not isinstance(node, dict):
            raise ValueError("every skin node must be an object")
        children = node.get("children", [])
        if not isinstance(children, list):
            raise ValueError("skin node children must be a list")
        for child in children:
            if type(child) is not int or not 0 <= child < len(nodes):
                raise ValueError("skin node child index is out of range")
            if child == parent_index or source_parents[child] is not None:
                raise ValueError("a skin node must have at most one parent")
            source_parents[child] = parent_index
    # Skin transforms are evaluated relative to the first skinned mesh node.
    # Ancestors shared by that node and the skeleton cancel from the palette;
    # only the branch below the mesh-space root belongs in its local rig.
    mesh_root = next((index for index, node in enumerate(nodes)
        if isinstance(node, dict) and node.get("skin") == 0), None)
    mesh_space_ancestors = set()
    ancestor = mesh_root
    while ancestor is not None:
        mesh_space_ancestors.add(ancestor)
        ancestor = source_parents[ancestor]
    animations = document.get("animations", [])
    animated_nodes = set()
    for animation in animations if isinstance(animations, list) else []:
        if not isinstance(animation, dict):
            continue
        channels = animation.get("channels", [])
        for channel in channels if isinstance(channels, list) else []:
            target = channel.get("target") if isinstance(channel, dict) else None
            node_index = target.get("node") if isinstance(target, dict) else None
            if type(node_index) is int:
                animated_nodes.add(node_index)

    source_locals = [cook_gltf_nodes.local_matrix(node) for node in nodes]
    world_cache: dict[int, tuple[float, ...]] = {}

    def world_matrix(node_index: int, visiting: set[int] | None = None) -> tuple[float, ...]:
        if node_index in world_cache:
            return world_cache[node_index]
        active = set() if visiting is None else visiting
        if node_index in active:
            raise ValueError("skin joint hierarchy contains a cycle")
        active.add(node_index)
        parent = source_parents[node_index]
        result = source_locals[node_index] if parent is None else cook_gltf_nodes.multiply(
            world_matrix(parent, active), source_locals[node_index])
        active.remove(node_index)
        world_cache[node_index] = result
        return result

    rig_nodes = set(joints)
    for joint_index in joints:
        ancestor = source_parents[joint_index]
        visited_ancestors = set()
        while ancestor is not None:
            if ancestor in visited_ancestors:
                raise ValueError("skin joint hierarchy contains a cycle")
            visited_ancestors.add(ancestor)
            if (ancestor not in joint_set and ancestor not in mesh_space_ancestors and
                    (cook_gltf_nodes.local_matrix(nodes[ancestor]) != cook_gltf_nodes.IDENTITY or
                     ancestor in animated_nodes)):
                rig_nodes.add(ancestor)
            ancestor = source_parents[ancestor]
    if len(rig_nodes) > MAX_RIG_NODES:
        raise ValueError(f"skin rig hierarchy exceeds {MAX_RIG_NODES} nodes")

    inverse_bind_matrices = _read_inverse_bind(document, buffer, skin, len(joints))

    mesh_world = world_matrix(mesh_root)
    mesh_inverse: tuple[float, ...] | None = None
    parents: dict[int, int | None] = {}
    basis_by_boundary: dict[int | None, int] = {}
    basis_records: dict[int, tuple[str, tuple[float, ...]]] = {}
    next_basis = len(nodes)
    for node_index in rig_nodes:
        ancestor = source_parents[node_index]
        while ancestor is not None and ancestor not in rig_nodes:
            ancestor = source_parents[ancestor]
        if ancestor is not None:
            parents[node_index] = ancestor
            continue

        boundary = source_parents[node_index]
        while boundary is not None and boundary not in mesh_space_ancestors:
            boundary = source_parents[boundary]
        if boundary == mesh_root:
            basis = cook_gltf_nodes.IDENTITY
        else:
            if mesh_inverse is None:
                mesh_inverse = _inverse_affine(mesh_world, "skinned mesh-space root")
            boundary_world = cook_gltf_nodes.IDENTITY if boundary is None else world_matrix(boundary)
            basis = cook_gltf_nodes.multiply(mesh_inverse, boundary_world)
        if all(abs(value - expected) <= 1.0e-6 for value, expected in zip(basis, cook_gltf_nodes.IDENTITY)):
            parents[node_index] = None
            continue
        if boundary not in basis_by_boundary:
            basis_id = next_basis
            next_basis += 1
            basis_by_boundary[boundary] = basis_id
            name = f"skin_mesh_basis_{boundary if boundary is not None else 'root'}"
            basis_records[basis_id] = (name, _rest_matrix_transform(basis, name))
            parents[basis_id] = None
        parents[node_index] = basis_by_boundary[boundary]

    if len(rig_nodes) + len(basis_records) > MAX_RIG_NODES:
        raise ValueError(f"skin rig hierarchy exceeds {MAX_RIG_NODES} nodes")
    ordered: list[int] = []
    visiting: set[int] = set()
    visited: set[int] = set()

    def visit(node_index: int) -> None:
        if node_index in visiting:
            raise ValueError("skin joint hierarchy contains a cycle")
        if node_index in visited:
            return
        visiting.add(node_index)
        parent = parents[node_index]
        if parent is not None:
            visit(parent)
        visiting.remove(node_index)
        visited.add(node_index)
        ordered.append(node_index)

    for node_index in (*basis_records, *joints):
        visit(node_index)
    ordered_index = {node: index for index, node in enumerate(ordered)}
    source_ordered_index = {node: ordered_index[node] for node in rig_nodes}
    cluster_joints = [source_ordered_index[node] for node in joints]
    rig_joints = []
    for node_index in ordered:
        parent = parents[node_index]
        if node_index in basis_records:
            name, rest = basis_records[node_index]
        else:
            node = document["nodes"][node_index]
            name = node.get("name", f"joint_{node_index}")
            if not isinstance(name, str) or not name:
                raise ValueError("skin rig node names must be nonempty strings")
            rest = _rest_transform(node, f"skin rig node {node_index}")
        rig_joints.append({"name": name, "parent": -1 if parent is None else ordered_index[parent], "rest": rest})
    animation_clips = cook_gltf_animation.normalize(document, buffer, source_ordered_index,
        [joint["rest"] for joint in rig_joints])
    return {"bone_names": [rig_joints[source_ordered_index[node]]["name"] for node in joints],
        "joints": rig_joints, "cluster_joints": cluster_joints,
        "inverse_bind_matrices": inverse_bind_matrices, "animation_clips": animation_clips,
        "source_node_indices": source_ordered_index,
        "placement_palettes": {mesh_node: {"palette_offset": 0, "palette_count": len(joints)}
            for mesh_node in (index for index, node in enumerate(nodes)
                if isinstance(node, dict) and node.get("skin") == 0)}}


def normalize(document: dict, buffer: bytes) -> dict | None:
    """Return a combined bounded rig, or None for an unskinned document.

    Multi-skin scenes receive one rig branch per skinned mesh placement. That
    keeps each joint hierarchy relative to its own mesh while letting the
    runtime use one armature and palette for the cooked scene package.
    """
    skins = document.get("skins", [])
    if not isinstance(skins, list) or not skins:
        return _normalize_single(document, buffer)
    nodes = document.get("nodes", [])
    has_static_mesh = isinstance(nodes, list) and any(isinstance(node, dict) and
        "mesh" in node and "skin" not in node for node in nodes)
    if len(skins) == 1 and not has_static_mesh:
        return _normalize_single(document, buffer)
    return _normalize_scene_skins(document, buffer)


def _normalize_scene_skins(document: dict, buffer: bytes) -> dict:
    """Combine skin rigs and static bind nodes for one cooked scene package."""
    skins = document.get("skins", [])
    nodes = document.get("nodes", [])
    if (not isinstance(nodes, list) or not nodes or len(skins) > MAX_JOINTS or
            any(not isinstance(node, dict) for node in nodes) or
            any(not isinstance(skin, dict) for skin in skins)):
        raise ValueError(f"runtime glTF cooker accepts 1 to {MAX_JOINTS} valid skins")

    mesh_nodes = [(index, node) for index, node in enumerate(nodes) if "mesh" in node]
    skin_placements: dict[int, list[int]] = {index: [] for index in range(len(skins))}
    skinned_nodes = []
    static_nodes = []
    for node_index, node in mesh_nodes:
        skin_index = node.get("skin")
        if skin_index is None:
            static_nodes.append((node_index, node))
            continue
        if type(skin_index) is not int or not 0 <= skin_index < len(skins):
            raise ValueError("skinned mesh placement must reference a declared skin")
        skin_placements[skin_index].append(node_index)
        skinned_nodes.append((node_index, node))
    if (not skinned_nodes or any(not placements for placements in skin_placements.values()) or
            not mesh_nodes):
        raise ValueError("every declared skin in a cooked scene must be used by a mesh placement")
    if any("skin" in node and "mesh" not in node for node in nodes):
        raise ValueError("only mesh nodes may reference a skin")

    bone_names: list[str] = []
    joints: list[dict] = []
    cluster_joints: list[int] = []
    inverse_bind_matrices: list[float] = []
    source_node_indices: dict[int, list[int]] = {}
    placement_palettes: dict[int, dict[str, int]] = {}
    for node_index, node in skinned_nodes:
        skin_index = node["skin"]
        rig_document = dict(document)
        rig_document["skins"] = [skins[skin_index]]
        rig_document["animations"] = []
        rig_document["nodes"] = [dict(value) for value in nodes]
        for other_index, other in enumerate(rig_document["nodes"]):
            if other_index == node_index:
                other["skin"] = 0
            elif "mesh" in other:
                other.pop("mesh", None)
                other.pop("skin", None)
            else:
                other.pop("skin", None)
        rig = _normalize_single(rig_document, buffer)
        if rig is None:
            raise ValueError("skinned mesh placement did not produce a rig")

        palette_offset = len(bone_names)
        if palette_offset + len(rig["bone_names"]) > MAX_JOINTS:
            raise ValueError(f"combined skin palette exceeds {MAX_JOINTS} bones")
        rig_offset = len(joints)
        prefix = f"placement_{node_index}_skin_{skin_index}::"
        bone_names.extend(prefix + name for name in rig["bone_names"])
        for joint in rig["joints"]:
            parent = joint["parent"]
            joints.append({"name": prefix + joint["name"],
                "parent": -1 if parent < 0 else rig_offset + parent, "rest": joint["rest"]})
        cluster_joints.extend(rig_offset + value for value in rig["cluster_joints"])
        inverse_bind_matrices.extend(rig["inverse_bind_matrices"])
        for source_node, rig_index in rig["source_node_indices"].items():
            source_node_indices.setdefault(source_node, []).append(rig_offset + rig_index)
        placement_palettes[node_index] = {
            "palette_offset": palette_offset, "palette_count": len(rig["bone_names"])}

    if static_nodes:
        if len(bone_names) >= MAX_JOINTS:
            raise ValueError(f"combined skin palette exceeds {MAX_JOINTS} bones")
        joint_index = len(joints)
        palette_index = len(bone_names)
        bind_name = "static_bind_space"
        rest = (0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 1.0)
        bone_names.append(bind_name)
        joints.append({"name": bind_name, "parent": -1, "rest": rest})
        cluster_joints.append(joint_index)
        inverse_bind_matrices.extend(GLTF_IDENTITY_MATRIX)
        for node_index, _ in static_nodes:
            placement_palettes[node_index] = {
                "palette_offset": palette_index, "palette_count": 1, "static_binding": True}

    if len(joints) > MAX_RIG_NODES:
        raise ValueError(f"combined skin rig exceeds {MAX_RIG_NODES} nodes")
    animation_clips = cook_gltf_animation.normalize(document, buffer, source_node_indices,
        [joint["rest"] for joint in joints])
    return {"bone_names": bone_names, "joints": joints, "cluster_joints": cluster_joints,
        "inverse_bind_matrices": inverse_bind_matrices, "animation_clips": animation_clips,
        "source_node_indices": source_node_indices, "placement_palettes": placement_palettes}
