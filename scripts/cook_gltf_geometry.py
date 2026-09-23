"""Normalize a static glTF scene into Elisa's bounded runtime mesh package."""

from __future__ import annotations

import base64
import hashlib
import math
from pathlib import Path
import struct

import cook_assets
import cook_gltf_animation
import cook_gltf_nodes
import cook_gltf_scene
import cook_gltf_skin
import gltf_tangent_frames
import cook_gltf_textures

# The native loader enforces the same bounds.
MAX_SUBSETS = 16
MAX_MATERIAL_SLOTS = 16
MAX_VERTICES = 2_000_000
MAX_INDICES = 15_000_000
MAX_MORPH_TARGETS = 32


# Render scene alpha modes. Alpha masking needs a base-color texture.
ALPHA_MODES = {"OPAQUE": 0, "MASK": 1, "BLEND": 2}
MATERIAL_KEYS = {"name", "pbrMetallicRoughness", "emissiveFactor", "alphaMode", "alphaCutoff", "doubleSided",
    "normalTexture", "occlusionTexture", "emissiveTexture"}
PBR_KEYS = {"baseColorFactor", "metallicFactor", "roughnessFactor", "baseColorTexture", "metallicRoughnessTexture"}
SLOT_MATERIAL_STRIDE = 48
DOUBLE_SIDED = 1
OCCLUSION = 2


def unit_factor(value, label: str) -> float:
    if type(value) not in (int, float) or not math.isfinite(value) or not 0.0 <= value <= 1.0:
        raise ValueError(f"material {label} must be a finite number in [0, 1]")
    return float(value)


def unit_factors(value, count: int, label: str) -> list[float]:
    if not isinstance(value, list) or len(value) != count:
        raise ValueError(f"material {label} must list {count} numbers")
    return [unit_factor(component, label) for component in value]


def slot_material(document: dict, material) -> tuple[bytes, list]:
    """Pack one glTF material's factors, with glTF defaults for absent ones,
    into a 48-byte slot record: base color, metallic, roughness, emissive,
    alpha cutoff, alpha mode, and flags (bit 0: double-sided, bit 1: occlusion
    enabled). Also return the image each runtime
    texture slot samples, or None."""
    if not isinstance(material, dict):
        raise ValueError("every glTF material must be an object")
    pbr = material.get("pbrMetallicRoughness", {})
    if not isinstance(pbr, dict):
        raise ValueError("material pbrMetallicRoughness must be an object")
    if set(material) - MATERIAL_KEYS or set(pbr) - PBR_KEYS:
        raise ValueError("runtime geometry cooker encountered unsupported material properties")
    images, occlusion = cook_gltf_textures.material_images(document, material, pbr)
    mode = material.get("alphaMode", "OPAQUE")
    if mode not in ALPHA_MODES:
        raise ValueError("material alphaMode must be OPAQUE, MASK, or BLEND")
    if mode == "MASK" and images[0] is None:
        raise ValueError("alpha-mask materials need a base-color texture")
    double_sided = material.get("doubleSided", False)
    if type(double_sided) is not bool:
        raise ValueError("material doubleSided must be a boolean")
    record = struct.pack("<10f2I",
        *unit_factors(pbr.get("baseColorFactor", [1.0, 1.0, 1.0, 1.0]), 4, "baseColorFactor"),
        unit_factor(pbr.get("metallicFactor", 1.0), "metallicFactor"),
        unit_factor(pbr.get("roughnessFactor", 1.0), "roughnessFactor"),
        *unit_factors(material.get("emissiveFactor", [0.0, 0.0, 0.0]), 3, "emissiveFactor"),
        unit_factor(material.get("alphaCutoff", 0.5), "alphaCutoff"),
        ALPHA_MODES[mode], (DOUBLE_SIDED if double_sided else 0) | (OCCLUSION if occlusion else 0))
    return record, images


def material_slots(document: dict, primitives: list) -> tuple[int, list[bytes], list]:
    """Return the mesh's material slot count, each declared material's slot
    record, and the images each slot samples: one slot per declared glTF
    material, or one unrecorded slot when the document declares none.

    Either every primitive binds a declared material or the document declares
    none. Material properties the runtime cannot reproduce are rejected rather
    than silently dropped.
    """
    materials = document.get("materials", [])
    if not isinstance(materials, list) or len(materials) > MAX_MATERIAL_SLOTS:
        raise ValueError(f"runtime geometry cooker accepts at most {MAX_MATERIAL_SLOTS} materials")
    slots = [slot_material(document, material) for material in materials]
    bindings = [primitive.get("material") for primitive in primitives]
    if not materials:
        if any(binding is not None for binding in bindings):
            raise ValueError("primitive binds a material the document does not declare")
        return 1, [], []
    for binding in bindings:
        if type(binding) is not int or not 0 <= binding < len(materials):
            raise ValueError("every primitive must bind one of the document's materials")
    for primitive in primitives:
        textured = any(image is not None for image in slots[primitive["material"]][1])
        if textured and "TEXCOORD_0" not in primitive.get("attributes", {}):
            raise ValueError("a primitive with a textured material needs TEXCOORD_0")
    return len(materials), [record for record, _ in slots], [images for _, images in slots]


def validate_static_geometry_source(document: dict, buffer: bytes) -> tuple:
    """Validate glTF input and normalize placement, skin, morph, animation, and material metadata."""
    allowed_extensions = {cook_gltf_scene.LIGHT_EXTENSION, cook_gltf_textures.KHR_TEXTURE_BASISU}
    if set(document.get("extensionsUsed", [])) - allowed_extensions or set(document.get("extensionsRequired", [])) - allowed_extensions:
        raise ValueError("runtime geometry cooker does not support glTF extensions")
    meshes = document.get("meshes", [])
    if not isinstance(meshes, list) or not 1 <= len(meshes) <= cook_gltf_nodes.MAX_NODES:
        raise ValueError(f"runtime geometry cooker requires 1 to {cook_gltf_nodes.MAX_NODES} meshes")
    every_primitive = []
    for mesh in meshes:
        if not isinstance(mesh, dict) or set(mesh) - {"name", "primitives", "weights"}:
            raise ValueError("runtime geometry cooker encountered unsupported mesh properties")
        primitives = mesh.get("primitives", [])
        if not isinstance(primitives, list) or not 1 <= len(primitives) <= MAX_SUBSETS:
            raise ValueError(f"runtime geometry cooker requires 1 to {MAX_SUBSETS} primitives")
        for primitive in primitives:
            if not isinstance(primitive, dict) or set(primitive) - {"attributes", "indices", "mode", "material", "targets"}:
                raise ValueError("runtime geometry cooker encountered unsupported primitive properties")
            attributes = primitive.get("attributes", {})
            if not isinstance(attributes, dict) or any(type(value) is not int for value in attributes.values()):
                raise ValueError("primitive attributes must name accessors by index")
            if set(attributes) - {"POSITION", "NORMAL", "TANGENT", "TEXCOORD_0", "JOINTS_0", "WEIGHTS_0"}:
                raise ValueError("runtime geometry cooker encountered an unsupported vertex attribute")
            if "TANGENT" in attributes and "NORMAL" not in attributes:
                raise ValueError("a TANGENT attribute needs NORMAL")
            has_joints = "JOINTS_0" in attributes
            if has_joints != ("WEIGHTS_0" in attributes):
                raise ValueError("primitives must provide JOINTS_0 and WEIGHTS_0 together")
            targets = primitive.get("targets", [])
            if not isinstance(targets, list) or len(targets) > MAX_MORPH_TARGETS:
                raise ValueError(f"runtime geometry cooker accepts at most {MAX_MORPH_TARGETS} morph targets")
            for target in targets:
                if (not isinstance(target, dict) or set(target) - {"POSITION", "NORMAL"} or
                        "POSITION" not in target or type(target["POSITION"]) is not int or
                        any(type(value) is not int for value in target.values())):
                    raise ValueError("morph targets must provide a POSITION accessor and optional NORMAL")
            if every_primitive and len(targets) != len(every_primitive[0].get("targets", [])):
                raise ValueError("all runtime primitives must use the same morph target count")
        every_primitive += primitives
    morph_count = len(every_primitive[0].get("targets", [])) if every_primitive else 0
    if any(len(primitive.get("targets", [])) != morph_count for primitive in every_primitive):
        raise ValueError("all runtime primitives must use the same morph target count")
    slot_count, slot_records, slot_images = material_slots(document, every_primitive)
    placement_records = cook_gltf_nodes.mesh_placement_records(document, len(meshes),
        allow_singular_mesh_transforms=bool(document.get("skins")))
    if morph_count == 0 and any("weights" in document["nodes"][node_index] or
            "weights" in meshes[mesh_index] for mesh_index, node_index, _ in placement_records):
        raise ValueError("mesh and node weights require morph targets")
    for mesh_index, node_index, authored_matrix in placement_records:
        skinned_placement = "skin" in document["nodes"][node_index]
        if not skinned_placement and cook_gltf_nodes.determinant(authored_matrix) == 0.0:
            raise ValueError("static mesh placement transforms must be invertible")
        for primitive in meshes[mesh_index]["primitives"]:
            has_joints = "JOINTS_0" in primitive.get("attributes", {})
            if has_joints != skinned_placement:
                raise ValueError("skinned placements require joint attributes; static placements must omit them")
    skin = cook_gltf_skin.normalize(document, buffer)
    ordered_index = {} if skin is None else skin["source_node_indices"]
    rest = [] if skin is None else [joint["rest"] for joint in skin["joints"]]
    animation_clips = cook_gltf_animation.normalize(document, buffer, ordered_index, rest,
        placement_records, morph_count)
    default_weights = cook_gltf_animation.morph_defaults(document, placement_records, morph_count)
    scene = cook_gltf_scene.normalize(document, buffer, placement_records)
    return (placement_records, slot_count, slot_records, slot_images, skin, morph_count,
        animation_clips, default_weights, scene)


def float_stream(document: dict, buffer: bytes, reference, type_name: str,
        count: int | None, label: str) -> bytes:
    accessors = document.get("accessors", [])
    if type(reference) is not int or not 0 <= reference < len(accessors):
        raise ValueError(f"{label} accessor index out of range")
    accessor = accessors[reference]
    if accessor["componentType"] != 5126 or accessor["type"] != type_name:
        raise ValueError(f"{label} must be float32 {type_name} values")
    if count is not None and accessor["count"] != count:
        raise ValueError(f"{label} count does not match the positions")
    data = cook_assets.accessor_bytes(document, buffer, reference)
    if not all(math.isfinite(value) for (value,) in struct.iter_unpack("<f", data)):
        raise ValueError(f"{label} data contains non-finite values")
    return data


def read_morph_targets(document: dict, buffer: bytes, targets: list[dict], vertex_count: int) -> list[dict]:
    result = []
    for index, target in enumerate(targets):
        positions = float_stream(document, buffer, target["POSITION"], "VEC3", vertex_count,
            f"morph target {index} positions")
        normals = None
        if "NORMAL" in target:
            normals = float_stream(document, buffer, target["NORMAL"], "VEC3", vertex_count,
                f"morph target {index} normals")
        result.append({"positions": positions, "normals": normals})
    return result


def read_vertices(document: dict, buffer: bytes, attributes: dict, targets: list[dict]) -> dict:
    if "POSITION" not in attributes:
        raise ValueError("triangle primitive is missing positions or indices")
    accessors = document.get("accessors", [])
    position_index = attributes["POSITION"]
    if type(position_index) is not int or not 0 <= position_index < len(accessors):
        raise ValueError("position accessor index out of range")
    vertex_count = accessors[position_index]["count"]
    if vertex_count <= 0 or vertex_count > MAX_VERTICES:
        raise ValueError("position count is empty or exceeds the runtime bound")
    positions = float_stream(document, buffer, position_index, "VEC3", None, "positions")
    if len(positions) != vertex_count * 12:
        raise ValueError("position data is malformed or non-finite")
    normals = None
    if "NORMAL" in attributes:
        normals = float_stream(document, buffer, attributes["NORMAL"], "VEC3", vertex_count, "normals")
    tangents = None
    if "TANGENT" in attributes:
        tangents = float_stream(document, buffer, attributes["TANGENT"], "VEC4", vertex_count, "tangents")
    uvs = bytes(vertex_count * 8)
    if "TEXCOORD_0" in attributes:
        uvs = float_stream(document, buffer, attributes["TEXCOORD_0"], "VEC2", vertex_count, "UVs")
    skin_indices = skin_weights = None
    if "JOINTS_0" in attributes:
        skin_indices, skin_weights = cook_gltf_skin.read_influences(document, buffer, attributes, vertex_count)
    return {"positions": positions, "normals": normals, "tangents": tangents, "uvs": uvs,
        "skin_indices": skin_indices, "skin_weights": skin_weights,
        "morph_targets": read_morph_targets(document, buffer, targets, vertex_count),
        "count": vertex_count, "triangles": []}


def read_indices(document: dict, buffer: bytes, primitive: dict, vertex_count: int) -> list[int]:
    accessors = document.get("accessors", [])
    reference = primitive.get("indices")
    if reference is None:
        raise ValueError("triangle primitive is missing positions or indices")
    if type(reference) is not int or not 0 <= reference < len(accessors):
        raise ValueError("index accessor index out of range")
    accessor = accessors[reference]
    if accessor["type"] != "SCALAR" or accessor["componentType"] not in (5121, 5123, 5125):
        raise ValueError("indices must be uint8, uint16, or uint32 scalars")
    index_count = accessor["count"]
    if index_count < 3 or index_count % 3 != 0 or index_count > MAX_INDICES:
        raise ValueError("index count is not a bounded triangle list")
    index_format = {5121: "<B", 5123: "<H", 5125: "<I"}[accessor["componentType"]]
    values = [value for (value,) in struct.iter_unpack(index_format,
        cook_assets.accessor_bytes(document, buffer, reference))]
    if any(value >= vertex_count for value in values):
        raise ValueError("index references a vertex outside the position stream")
    return values


def generated_normals(positions: bytes, vertex_count: int, indices: list[int]) -> bytes:
    points = list(struct.iter_unpack("<3f", positions))
    accumulators = [[0.0, 0.0, 0.0] for _ in range(vertex_count)]
    for triangle in range(0, len(indices), 3):
        a, b, c = (points[indices[triangle + offset]] for offset in range(3))
        ab = (b[0] - a[0], b[1] - a[1], b[2] - a[2])
        ac = (c[0] - a[0], c[1] - a[1], c[2] - a[2])
        cross = (ab[1] * ac[2] - ab[2] * ac[1],
            ab[2] * ac[0] - ab[0] * ac[2], ab[0] * ac[1] - ab[1] * ac[0])
        for vertex in indices[triangle:triangle + 3]:
            for axis in range(3):
                accumulators[vertex][axis] += cross[axis]
    packed = bytearray()
    for normal in accumulators:
        length = math.sqrt(sum(component * component for component in normal))
        if length <= 1e-20:
            raise ValueError("cannot generate a normal for a degenerate vertex")
        packed += struct.pack("<3f", *(component / length for component in normal))
    return bytes(packed)


def normalized_geometry(document: dict, buffer: bytes):
    # Node transforms bake into placement streams; material subsets and
    # textures stay grouped. Identical accessors share data within a placement.
    # Skin streams retain remapped influences and parent-ordered rig transforms.
    (placements, slot_count, slot_records, slot_images, skin, morph_count,
        animation_clips, morph_default_weights, scene) = validate_static_geometry_source(document, buffer)
    sources: dict[tuple, dict] = {}
    blocks: dict[tuple, dict] = {}
    placed = []
    placement_ranges = [{"vertex_start": None, "vertex_count": 0,
        "index_start": None, "index_count": 0, "subset_start": None, "subset_count": 0}
        for _ in placements]
    vertex_total = index_total = 0
    for placement, (mesh, node_index, authored_matrix) in enumerate(placements):
        # glTF explicitly ignores the transform of a node that instances a
        # skinned mesh. Keep its metadata for scene queries, but do not bake it
        # into vertex streams, indices, normals, tangents, or morph deltas.
        skinned_placement = skin is not None and "skin" in document["nodes"][node_index]
        matrix = cook_gltf_nodes.IDENTITY if skinned_placement else authored_matrix
        block_for_position: dict = {}
        for primitive in document["meshes"][mesh]["primitives"]:
            if primitive.get("mode", 4) != 4:
                raise ValueError("runtime geometry cooker supports triangle primitives only")
            attributes = primitive.get("attributes", {})
            targets = primitive.get("targets", [])
            target_key = tuple(tuple(sorted(target.items())) for target in targets)
            key = (tuple(sorted(attributes.items())), target_key)
            if block_for_position.setdefault(attributes.get("POSITION"), key) != key:
                raise ValueError("primitives that share positions must share every vertex attribute")
            if key not in sources:
                sources[key] = read_vertices(document, buffer, attributes, targets)
            block = (placement, key)
            if block not in blocks:
                vertex_total += sources[key]["count"]
                if vertex_total > MAX_VERTICES:
                    raise ValueError("position count is empty or exceeds the runtime bound")
                block_skin_indices = []
                block_skin_weights = []
                if skin is not None:
                    palette = skin["placement_palettes"].get(node_index)
                    if palette is None:
                        raise ValueError("skinned mesh placement has no normalized palette")
                    if palette.get("static_binding", False):
                        block_skin_indices = [palette["palette_offset"]] * (sources[key]["count"] * 4)
                        block_skin_weights = [value for _ in range(sources[key]["count"])
                            for value in (1.0, 0.0, 0.0, 0.0)]
                    else:
                        source_indices = sources[key]["skin_indices"]
                        if any(index >= palette["palette_count"] for index in source_indices):
                            raise ValueError("skin joint index is outside the declared skin")
                        block_skin_indices = [index + palette["palette_offset"] for index in source_indices]
                        block_skin_weights = sources[key]["skin_weights"]
                blocks[block] = {"source": sources[key], "matrix": matrix, "triangles": [],
                    "skin_indices": block_skin_indices, "skin_weights": block_skin_weights}
            values = read_indices(document, buffer, primitive, sources[key]["count"])
            index_total += len(values)
            if index_total > MAX_INDICES:
                raise ValueError("index count is not a bounded triangle list")
            values = cook_gltf_nodes.placed_indices(values, matrix)
            blocks[block]["triangles"].extend(values)
            placed.append((placement, block, values, primitive.get("material", 0)))

    positions, normals, tangents, uvs = bytearray(), bytearray(), bytearray(), bytearray()
    morph_positions = [bytearray() for _ in range(morph_count)]
    morph_normals = [bytearray() for _ in range(morph_count)]
    skin_indices: list[int] = []
    skin_weights: list[float] = []
    base_vertex: dict[tuple, int] = {}
    for key, block in blocks.items():
        source, matrix = block["source"], block["matrix"]
        base_vertex[key] = len(positions) // 12
        placement = key[0]
        placement_ranges[placement]["vertex_start"] = (
            base_vertex[key] if placement_ranges[placement]["vertex_start"] is None else
            placement_ranges[placement]["vertex_start"])
        placement_ranges[placement]["vertex_count"] += source["count"]
        world = cook_gltf_nodes.transform_points(source["positions"], matrix)
        positions += world
        transformed_normals = cook_gltf_nodes.transform_normals(source["normals"], matrix) \
            if source["normals"] is not None else \
            generated_normals(world, source["count"], block["triangles"])
        normals += transformed_normals
        tangents += gltf_tangent_frames.transformed(source["tangents"], transformed_normals, matrix) \
            if source["tangents"] is not None else \
            gltf_tangent_frames.generated(world, transformed_normals, source["uvs"], source["count"], block["triangles"])
        uvs += source["uvs"]
        for morph_index, target in enumerate(source["morph_targets"]):
            morph_positions[morph_index] += cook_gltf_nodes.transform_vectors(target["positions"], matrix)
            if target["normals"] is not None:
                morph_normals[morph_index] += cook_gltf_nodes.transform_normal_deltas(target["normals"], matrix)
        if skin is not None:
            skin_indices += block["skin_indices"]
            skin_weights += block["skin_weights"]

    indices = bytearray()
    subsets = []
    for placement, block, values, slot in placed:
        start = len(indices) // 4
        placement_range = placement_ranges[placement]
        if placement_range["index_start"] is None:
            placement_range["index_start"] = start
        indices += struct.pack(f"<{len(values)}I", *(value + base_vertex[block] for value in values))
        if subsets and subsets[-1][2] == slot:
            subsets[-1] = (subsets[-1][0], subsets[-1][1] + len(values), slot)
        else:
            subsets.append((start, len(values), slot))
        placement_range["index_count"] += len(values)
    for placement_range in placement_ranges:
        first = placement_range["index_start"]
        last = first + placement_range["index_count"]
        overlaps = [index for index, (start, count, _) in enumerate(subsets)
            if start < last and start + count > first]
        if not overlaps:
            raise ValueError("mesh placement produced no subset range")
        placement_range["subset_start"] = overlaps[0]
        placement_range["subset_count"] = len(overlaps)
    if len(subsets) > MAX_SUBSETS:
        raise ValueError(f"placed primitives need more than {MAX_SUBSETS} material subsets")
    slot_textures, images = cook_gltf_textures.cooked_textures(document, buffer, slot_images)
    morph_targets = [{"positions": bytes(morph_positions[index]),
        "normals": bytes(morph_normals[index]) if morph_normals[index] else None}
        for index in range(morph_count)]
    if any(target["normals"] is not None for target in morph_targets) and any(
            target["normals"] is None for target in morph_targets):
        raise ValueError("all morph targets must provide normals or none may provide them")
    return {"positions": bytes(positions), "normals": bytes(normals), "tangents": bytes(tangents), "uvs": bytes(uvs),
        "indices": bytes(indices), "vertex_count": len(positions) // 12,
        "index_count": len(indices) // 4, "subsets": subsets, "material_slots": slot_count,
        "slot_materials": b"".join(slot_records), "slot_textures": slot_textures, "images": images,
        "skin": skin, "animation_clips": animation_clips,
        "morph_default_weights": morph_default_weights,
        "skin_indices": skin_indices, "skin_weights": skin_weights,
        "morph_targets": morph_targets, "scene": {**scene, "mesh_placements": [
            {**placement, **placement_ranges[index]}
            for index, placement in enumerate(scene["mesh_placements"])]}}


def placed_counts(document: dict, *, skinned: bool = False) -> dict:
    """Triangles and positions summed over node placements, counting a
    POSITION accessor once per placement, recounted from the accessors."""
    placements = cook_gltf_nodes.mesh_placements(document, len(document["meshes"]),
        allow_singular_mesh_transforms=skinned)
    accessors = document["accessors"]
    triangles = positions = 0
    for mesh, _ in placements:
        primitives = document["meshes"][mesh]["primitives"]
        triangles += sum(accessors[primitive["indices"]]["count"] // 3 for primitive in primitives)
        positions += sum(accessors[reference]["count"]
            for reference in {primitive["attributes"]["POSITION"] for primitive in primitives})
    return {"triangles": triangles, "positions": positions}


def safe_asset_path(value: str) -> str:
    if (not value or len(value) > 4096 or value.startswith("/") or "\\" in value or
            "\n" in value or "\r" in value or "\0" in value or
            any(part in ("", ".", "..") for part in value.split("/"))):
        raise ValueError("asset path must be a safe project-relative path without `..`")
    return value


def subset_lines(geometry: dict) -> list[str]:
    """Subset and slot material records. A package whose one subset covers
    every index with slot 0 and whose source declares no materials omits them,
    so single-primitive packages keep their earlier bytes."""
    subsets = geometry["subsets"]
    slot_materials = geometry["slot_materials"]
    if geometry["material_slots"] == 1 and len(subsets) == 1 and not slot_materials:
        return []
    packed = b"".join(struct.pack("<3I", *subset) for subset in subsets)
    lines = [
        f"material_slots={geometry['material_slots']}",
        f"subset_count={len(subsets)}",
        "subset_stride=12",
        "subsets_b64=" + base64.b64encode(packed).decode("ascii"),
    ]
    if slot_materials:
        lines += [f"slot_material_stride={SLOT_MATERIAL_STRIDE}",
            "slot_materials_b64=" + base64.b64encode(slot_materials).decode("ascii")]
    return lines + cook_gltf_textures.texture_lines(geometry["slot_textures"], geometry["images"])


def tangent_lines(geometry: dict) -> list[str]:
    if not geometry["tangents"]:
        return []
    return ["tangent_stride=16",
        "tangents_b64=" + base64.b64encode(geometry["tangents"]).decode("ascii")]


def _name_bytes(names: list[str]) -> bytes:
    packed = bytearray()
    for name in names:
        encoded = name.encode("utf-8")
        if len(encoded) > 0xFFFFFFFF:
            raise ValueError("skin name is too long")
        packed += struct.pack("<I", len(encoded)) + encoded
    return bytes(packed)


def skin_lines(geometry: dict) -> list[str]:
    skin = geometry["skin"]
    if skin is None:
        return []
    indices = geometry["skin_indices"]
    weights = geometry["skin_weights"]
    bones = skin["bone_names"]
    joints = skin["joints"]
    cluster_joints = skin["cluster_joints"]
    inverse_bind_matrices = skin.get("inverse_bind_matrices")
    if (len(indices) != geometry["vertex_count"] * 4 or len(weights) != len(indices) or
            len(bones) != len(cluster_joints) or len(bones) > cook_gltf_skin.MAX_JOINTS or
            not joints or len(joints) > cook_gltf_skin.MAX_RIG_NODES or
            (inverse_bind_matrices is not None and len(inverse_bind_matrices) != len(bones) * 16)):
        raise ValueError("normalized skin streams do not match the mesh")
    parents = [joint["parent"] for joint in joints]
    rests = [component for joint in joints for component in joint["rest"]]
    joint_names = [joint["name"] for joint in joints]
    lines = [
        f"skin_bones={len(bones)}", "skin_indices_stride=16", "skin_weights_stride=16",
        "skin_indices_b64=" + base64.b64encode(struct.pack(f"<{len(indices)}I", *indices)).decode("ascii"),
        "skin_weights_b64=" + base64.b64encode(struct.pack(f"<{len(weights)}f", *weights)).decode("ascii"),
        "skin_names_b64=" + base64.b64encode(_name_bytes(bones)).decode("ascii"),
        f"skin_joints={len(joints)}", "skin_joint_parent_stride=4", "skin_joint_rest_stride=40",
        "skin_joint_parents_b64=" + base64.b64encode(struct.pack(f"<{len(parents)}i", *parents)).decode("ascii"),
        "skin_joint_rest_b64=" + base64.b64encode(struct.pack(f"<{len(rests)}f", *rests)).decode("ascii"),
        "skin_joint_names_b64=" + base64.b64encode(_name_bytes(joint_names)).decode("ascii"),
        "skin_cluster_joints_stride=4",
        "skin_cluster_joints_b64=" + base64.b64encode(
            struct.pack(f"<{len(cluster_joints)}I", *cluster_joints)).decode("ascii"),
    ]
    if inverse_bind_matrices is not None:
        lines += ["skin_inverse_bind_stride=64",
            "skin_inverse_bind_matrices_b64=" + base64.b64encode(
                struct.pack(f"<{len(inverse_bind_matrices)}f", *inverse_bind_matrices)).decode("ascii")]
    return lines


def morph_lines(geometry: dict) -> list[str]:
    targets = geometry.get("morph_targets", [])
    if not targets:
        return []
    if len(targets) > MAX_MORPH_TARGETS:
        raise ValueError("normalized morph target count exceeds the runtime bound")
    lines = [f"morph_targets={len(targets)}", "morph_target_position_stride=12"]
    defaults = geometry.get("morph_default_weights", [])
    if len(defaults) != len(geometry["scene"]["mesh_placements"]) * len(targets):
        raise ValueError("morph default weights do not match the mesh placements")
    lines += ["morph_default_weights_stride=4", "morph_default_weights_b64=" + base64.b64encode(
        struct.pack(f"<{len(defaults)}f", *defaults)).decode("ascii")]
    has_normals = all(target["normals"] is not None for target in targets)
    if has_normals:
        lines.append("morph_target_normal_stride=12")
    for index, target in enumerate(targets):
        if len(target["positions"]) != geometry["vertex_count"] * 12:
            raise ValueError("normalized morph position stream does not match the mesh")
        lines.append(f"morph_{index}_positions_b64=" + base64.b64encode(target["positions"]).decode("ascii"))
        if has_normals:
            if len(target["normals"]) != geometry["vertex_count"] * 12:
                raise ValueError("normalized morph normal stream does not match the mesh")
            lines.append(f"morph_{index}_normals_b64=" + base64.b64encode(target["normals"]).decode("ascii"))
    return lines


def scene_lines(geometry: dict) -> list[str]:
    return cook_gltf_scene.lines(geometry.get("scene", {"cameras": [], "lights": []}))


def cook_geometry_package(source_path: Path, asset_path: str, output_path: Path,
        allow_textures: bool = False) -> tuple[Path, dict]:
    """Write the mesh package. A textured source cooks only when the caller
    will bundle the returned images beside it."""
    asset_path = safe_asset_path(asset_path)
    source_path = source_path.expanduser().resolve(strict=True)
    if not source_path.is_file() or source_path.stat().st_size == 0 or source_path.stat().st_size > cook_assets.MAX_DOCUMENT_BYTES:
        raise ValueError("glTF source is not a regular file within the 64 MiB source bound")
    data = source_path.read_bytes()
    document = cook_assets.read_gltf(data)
    geometry = normalized_geometry(document, cook_assets.source_bytes(source_path.parent, document))
    if geometry["images"] and not allow_textures:
        raise ValueError("material textures need an .elpk bundle output")
    counts = placed_counts(document, skinned=geometry["skin"] is not None)
    if (counts["triangles"] <= 0 or geometry["index_count"] != counts["triangles"] * 3 or
            geometry["vertex_count"] != counts["positions"] or
            cook_assets.normalized_counts(document)["bounds"] is None):
        raise ValueError("normalized geometry does not match the declared source counts")
    digest = hashlib.sha256(data).hexdigest()
    output_path = output_path.expanduser().resolve()
    output_path.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "format=" + ("elisa-cooked-v3" if geometry["skin"] is not None or geometry["animation_clips"]
            else "elisa-cooked-v2"),
        f"source={asset_path}",
        f"source_sha256={digest}",
        f"triangles={counts['triangles']}",
        f"positions={geometry['vertex_count']}",
        f"indices={geometry['index_count']}",
        "position_stride=12",
        "normal_stride=12",
        "uv_stride=8",
        "index_stride=4",
        *subset_lines(geometry),
        *tangent_lines(geometry),
        *skin_lines(geometry),
        *cook_gltf_animation.package_lines(geometry["animation_clips"],
            0 if geometry["skin"] is None else len(geometry["skin"]["joints"]),
            len(geometry["scene"]["mesh_placements"]), len(geometry["morph_targets"])),
        *morph_lines(geometry),
        *scene_lines(geometry),
        "positions_b64=" + base64.b64encode(geometry["positions"]).decode("ascii"),
        "normals_b64=" + base64.b64encode(geometry["normals"]).decode("ascii"),
        "uvs_b64=" + base64.b64encode(geometry["uvs"]).decode("ascii"),
        "indices_b64=" + base64.b64encode(geometry["indices"]).decode("ascii"),
    ]
    package_bytes = ("\n".join(lines) + "\n").encode("utf-8")
    if len(package_bytes) > 64 * 1024 * 1024:
        raise ValueError("cooked geometry package exceeds the 64 MiB runtime limit")
    output_path.write_bytes(package_bytes)
    return output_path, {"triangles": counts["triangles"], "positions": geometry["vertex_count"],
        "indices": geometry["index_count"], "subsets": len(geometry["subsets"]),
        "material_slots": geometry["material_slots"],
        "slot_materials": len(geometry["slot_materials"]) // SLOT_MATERIAL_STRIDE, "source_sha256": digest,
        "images": dict(geometry["images"]), "morph_targets": len(geometry.get("morph_targets", [])),
        "cameras": len(geometry.get("scene", {}).get("cameras", [])),
        "lights": len(geometry.get("scene", {}).get("lights", []))}
