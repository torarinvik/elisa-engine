"""Generate and validate the bounded multi-material glTF skin fixture."""

from __future__ import annotations

import base64
from copy import deepcopy
import json
from pathlib import Path
import struct
import sys
import tempfile

import cook_assets
import cook_gltf_animation
import cook_gltf_geometry
import cook_gltf_nodes
import cook_gltf_skin


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "test/fixtures/multi_material_panel.gltf"
ASSET_PATH = "test/generated/multi_material_skinned_panel.gltf"

ROOT_INVERSE_BIND = (1.0, 0.0, 0.0, 0.0,
    0.0, 1.0, 0.0, 0.0,
    0.0, 0.0, 1.0, 0.0,
    -2.0, 0.0, 0.25, 1.0)
TIP_INVERSE_BIND = (1.0, 0.0, 0.0, 0.0,
    0.0, 1.0, 0.0, 0.0,
    0.0, 0.0, 1.0, 0.0,
    -2.0, -1.0, 0.25, 1.0)
INVERSE_BIND_MATRICES = ROOT_INVERSE_BIND + TIP_INVERSE_BIND
GLTF_IDENTITY_MATRIX = (1.0, 0.0, 0.0, 0.0,
    0.0, 1.0, 0.0, 0.0,
    0.0, 0.0, 1.0, 0.0,
    0.0, 0.0, 0.0, 1.0)
DEFAULT_INVERSE_BIND_MATRICES = GLTF_IDENTITY_MATRIX * 2


def _append(document: dict, buffer: bytearray, payload: bytes, component: int,
        value_type: str, count: int) -> int:
    offset = len(buffer)
    if offset % 4:
        buffer += bytes(4 - offset % 4)
        offset = len(buffer)
    buffer += payload
    view = len(document["bufferViews"])
    document["bufferViews"].append({"buffer": 0, "byteOffset": offset, "byteLength": len(payload)})
    document["accessors"].append({"bufferView": view, "componentType": component,
        "count": count, "type": value_type})
    return len(document["accessors"]) - 1


def generated_document(inverse_bind_matrices: tuple[float, ...] | None = INVERSE_BIND_MATRICES) -> dict:
    document = cook_assets.read_gltf(SOURCE.read_bytes())
    buffer = bytearray(cook_assets.source_bytes(SOURCE.parent, document))
    primitive_counts = [8, 4, 8]
    influence_accessors = {}
    for primitive, count in zip(document["meshes"][0]["primitives"], primitive_counts):
        if count in influence_accessors:
            joint_accessor, weight_accessor = influence_accessors[count]
            primitive["attributes"]["JOINTS_0"] = joint_accessor
            primitive["attributes"]["WEIGHTS_0"] = weight_accessor
            continue
        joints = []
        for vertex in range(count):
            joint = 1 if vertex >= count // 2 and count > 4 else 0
            joints.extend((joint, 0, 0, 0))
        weights = (1.0, 0.0, 0.0, 0.0) * count
        joint_accessor = _append(document, buffer, struct.pack(f"<{count * 4}H", *joints), 5123, "VEC4", count)
        weight_accessor = _append(document, buffer, struct.pack(f"<{count * 4}f", *weights), 5126, "VEC4", count)
        influence_accessors[count] = (joint_accessor, weight_accessor)
        primitive["attributes"]["JOINTS_0"] = joint_accessor
        primitive["attributes"]["WEIGHTS_0"] = weight_accessor
    morph_accessors = {}
    for primitive, count in zip(document["meshes"][0]["primitives"], primitive_counts):
        if count not in morph_accessors:
            deltas = (0.0, 0.0, 0.125) * count
            morph_accessors[count] = _append(document, buffer,
                struct.pack(f"<{count * 3}f", *deltas), 5126, "VEC3", count)
        primitive["targets"] = [{"POSITION": morph_accessors[count]}]
    inverse_bind = None
    if inverse_bind_matrices is not None:
        if len(inverse_bind_matrices) % 16 != 0:
            raise ValueError("inverse bind fixture needs whole MAT4 values")
        joint_count = len(inverse_bind_matrices) // 16
        inverse_bind = _append(document, buffer,
            struct.pack(f"<{len(inverse_bind_matrices)}f", *inverse_bind_matrices), 5126, "MAT4", joint_count)
    input_accessor = _append(document, buffer, struct.pack("<2f", 0.0, 1.0), 5126, "SCALAR", 2)
    output_accessor = _append(document, buffer, struct.pack("<6f", 0.0, 1.0, 0.0, 0.0, 2.0, 0.0),
        5126, "VEC3", 2)
    document["buffers"][0]["byteLength"] = len(buffer)
    document["buffers"][0]["uri"] = "data:application/octet-stream;base64," + base64.b64encode(buffer).decode("ascii")
    document["nodes"][0].update({"skin": 0, "translation": [0.0, 0.0, 0.75]})
    document["nodes"] += [{"name": "root", "translation": [2.0, 0.0, 0.0], "children": [2]},
        {"name": "tip", "translation": [0.0, 1.0, 0.0]}]
    document["nodes"].append({"name": "second_panel_placement", "mesh": 0, "skin": 0,
        "translation": [3.0, 0.0, 0.0],
        "rotation": [0.0, 0.3826834323650898, 0.0, 0.9238795325112867],
        "scale": [2.0, 1.0, 0.5]})
    document["nodes"].append({"name": "skeleton_offset", "children": [1],
        "translation": [0.0, 0.0, 0.5]})
    document["nodes"].append({"name": "shared_scene_root", "children": [0, 4],
        "translation": [0.0, 0.0, 0.25]})
    document["scenes"][document["scene"]]["nodes"] = [5, 3]
    skin = {"name": "panel_rig", "joints": [1, 2], "skeleton": 1}
    if inverse_bind is not None:
        skin["inverseBindMatrices"] = inverse_bind
    document["skins"] = [skin]
    document["animations"] = [{"name": "lift", "samplers": [{"input": input_accessor,
        "output": output_accessor, "interpolation": "LINEAR"}], "channels": [{"sampler": 0,
        "target": {"node": 2, "path": "translation"}}]}]
    return document


def write_package(output: Path,
        inverse_bind_matrices: tuple[float, ...] | None = INVERSE_BIND_MATRICES) -> tuple[Path, dict]:
    with tempfile.TemporaryDirectory(prefix="elisa-gltf-skin-") as temporary:
        source = Path(temporary) / "multi_material_skinned_panel.gltf"
        source.write_text(json.dumps(generated_document(inverse_bind_matrices), separators=(",", ":")),
            encoding="utf-8")
        return cook_gltf_geometry.cook_geometry_package(source, ASSET_PATH, output)


def write_large_rig_package(output: Path, rig_node_count: int) -> tuple[Path, dict]:
    """Cook a bounded rig with more transform nodes than palette bones."""
    if not 3 <= rig_node_count <= cook_gltf_skin.MAX_RIG_NODES:
        raise ValueError("large rig fixture node count is outside the runtime bound")
    document = generated_document()
    helper_count = rig_node_count - len(document["skins"][0]["joints"]) - 1
    parent = 4
    for helper_index in range(1, helper_count):
        child = len(document["nodes"])
        document["nodes"][parent]["children"] = [child]
        document["nodes"].append({"name": f"skeleton_offset_{helper_index}",
            "translation": [0.0, 0.0, 0.001]})
        parent = child
    document["nodes"][parent]["children"] = [1]
    with tempfile.TemporaryDirectory(prefix="elisa-gltf-large-rig-") as temporary:
        source = Path(temporary) / "large_rig.gltf"
        source.write_text(json.dumps(document, separators=(",", ":")), encoding="utf-8")
        return cook_gltf_geometry.cook_geometry_package(source, ASSET_PATH, output)


def inverse_bind_defaults_self_test(temporary: Path) -> int:
    """Keep glTF's omitted-matrix default explicit at the cooker boundary."""
    package, _ = write_package(temporary / "default-inverse-bind.pkg", None)
    fields = dict(line.split("=", 1) for line in package.read_text(encoding="utf-8").splitlines())
    expected = struct.pack("<32f", *DEFAULT_INVERSE_BIND_MATRICES)
    if (fields.get("skin_inverse_bind_stride") != "64" or
            base64.b64decode(fields.get("skin_inverse_bind_matrices_b64", "")) != expected):
        print("glTF skin self-test failed: omitted inverseBindMatrices did not become identity matrices",
            file=sys.stderr)
        return 1

    extra = INVERSE_BIND_MATRICES + GLTF_IDENTITY_MATRIX
    document = generated_document(extra)
    buffer = cook_assets.source_bytes(temporary, cook_assets.read_gltf(
        json.dumps(document, separators=(",", ":")).encode("utf-8")))
    normalized = cook_gltf_geometry.normalized_geometry(document, buffer)
    if normalized["skin"]["inverse_bind_matrices"] != list(INVERSE_BIND_MATRICES):
        print("glTF skin self-test failed: surplus inverse bind entries changed palette order",
            file=sys.stderr)
        return 1
    return 0


def skinned_mesh_transform_self_test(temporary: Path) -> int:
    """Skinned node transforms remain metadata and never alter mesh streams."""
    document = generated_document()
    document["nodes"][3].pop("translation")
    document["nodes"][3].pop("rotation")
    document["nodes"][3].pop("scale")
    buffer = cook_assets.source_bytes(Path(temporary), cook_assets.read_gltf(
        json.dumps(document, separators=(",", ":")).encode("utf-8")))
    baseline = cook_gltf_geometry.normalized_geometry(document, buffer)

    transformed = deepcopy(document)
    transformed["nodes"][3].update({"translation": [1.25, -0.5, 2.0],
        "rotation": [0.0, 0.3826834323650898, 0.0, 0.9238795325112867],
        "scale": [1.5, 0.75, 2.0]})
    transformed_buffer = cook_assets.source_bytes(Path(temporary), cook_assets.read_gltf(
        json.dumps(transformed, separators=(",", ":")).encode("utf-8")))
    cooked = cook_gltf_geometry.normalized_geometry(transformed, transformed_buffer)
    unchanged_streams = ("positions", "normals", "tangents", "uvs", "indices", "skin",
        "skin_indices", "skin_weights", "morph_targets")
    if any(cooked[key] != baseline[key] for key in unchanged_streams):
        print("glTF skin self-test failed: skinned mesh node transform changed cooked geometry",
            file=sys.stderr)
        return 1
    expected_transform = cook_gltf_nodes.local_matrix(transformed["nodes"][3])
    if cooked["scene"]["mesh_placements"][1]["transform"] != expected_transform:
        print("glTF skin self-test failed: ignored mesh transform was lost from placement metadata",
            file=sys.stderr)
        return 1
    collapsed = deepcopy(document)
    collapsed["nodes"][3]["scale"] = [0.0, 1.0, 1.0]
    collapsed_buffer = cook_assets.source_bytes(Path(temporary), cook_assets.read_gltf(
        json.dumps(collapsed, separators=(",", ":")).encode("utf-8")))
    collapsed_geometry = cook_gltf_geometry.normalized_geometry(collapsed, collapsed_buffer)
    if any(collapsed_geometry[key] != baseline[key] for key in unchanged_streams):
        print("glTF skin self-test failed: ignored zero-scale mesh transform changed cooked geometry",
            file=sys.stderr)
        return 1
    collapsed_source = Path(temporary) / "zero-scale-skinned-node.gltf"
    collapsed_source.write_text(json.dumps(collapsed, separators=(",", ":")), encoding="utf-8")
    try:
        cook_gltf_geometry.cook_geometry_package(collapsed_source, ASSET_PATH,
            Path(temporary) / "zero-scale-skinned-node.pkg")
    except (ValueError, KeyError, IndexError, TypeError) as error:
        print(f"glTF skin self-test failed: ignored zero-scale node was rejected: {error}",
            file=sys.stderr)
        return 1
    return 0


def animation_limit_self_test(temporary: Path) -> int:
    """Keep the glTF normalizer, package writer, and native limit aligned."""
    document = generated_document()
    template = document["animations"][0]
    document["animations"] = [dict(template, name=f"clip_{index}")
        for index in range(cook_gltf_animation.MAX_CLIPS)]
    source = temporary / "sixteen_clips.gltf"
    source.write_text(json.dumps(document, separators=(",", ":")), encoding="utf-8")
    try:
        package, _ = cook_gltf_geometry.cook_geometry_package(source, ASSET_PATH,
            temporary / "sixteen_clips.pkg")
    except (ValueError, KeyError, IndexError, TypeError) as error:
        print(f"glTF skin self-test failed: sixteen clips were rejected: {error}", file=sys.stderr)
        return 1
    fields = dict(line.split("=", 1) for line in package.read_text(encoding="utf-8").splitlines())
    if fields.get("animation_clips") != str(cook_gltf_animation.MAX_CLIPS):
        print("glTF skin self-test failed: sixteen clips were not retained", file=sys.stderr)
        return 1
    document["animations"].append(dict(template, name="clip_overflow"))
    overflow_buffer = cook_assets.source_bytes(temporary, cook_assets.read_gltf(
        json.dumps(document, separators=(",", ":")).encode("utf-8")))
    try:
        cook_gltf_geometry.normalized_geometry(document, overflow_buffer)
    except ValueError:
        return 0
    print("glTF skin self-test failed: accepted a seventeenth animation clip", file=sys.stderr)
    return 1


def self_test(temporary: Path) -> int:
    if animation_limit_self_test(temporary) != 0:
        return 1
    if inverse_bind_defaults_self_test(temporary) != 0:
        return 1
    if skinned_mesh_transform_self_test(temporary) != 0:
        return 1
    first, result = write_package(temporary / "first.pkg")
    second, second_result = write_package(temporary / "second.pkg")
    sections = dict(line.split("=", 1) for line in first.read_text(encoding="utf-8").splitlines())
    required = {
        "format": "elisa-cooked-v3", "material_slots": "2", "subset_count": "5",
        "skin_bones": "2", "skin_joints": "4", "animation_clips": "1", "morph_targets": "1",
        "mesh_placement_count": "2", "skin_inverse_bind_stride": "64",
    }
    if (result != second_result or first.read_bytes() != second.read_bytes() or
            any(sections.get(key) != value for key, value in required.items()) or
            len(base64.b64decode(sections["skin_indices_b64"])) != 24 * 16 or
            len(base64.b64decode(sections["skin_joint_rest_b64"])) != 4 * 40 or
            struct.unpack("<4i", base64.b64decode(sections["skin_joint_parents_b64"])) != (-1, 0, 1, 2) or
            struct.unpack("<2I", base64.b64decode(sections["skin_cluster_joints_b64"])) != (2, 3) or
            struct.unpack("<10f", base64.b64decode(sections["skin_joint_rest_b64"])[:40]) !=
                (0.0, 0.0, -0.75, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 1.0) or
            base64.b64decode(sections["skin_inverse_bind_matrices_b64"]) !=
                struct.pack("<32f", *INVERSE_BIND_MATRICES) or
            sections.get("animation_0_sample_rate") != "30" or
            sections.get("animation_0_frames") != "31"):
        print("glTF skin self-test failed: package is unstable or incomplete", file=sys.stderr)
        return 1
    document = generated_document()
    buffer = cook_assets.source_bytes(Path(temporary), cook_assets.read_gltf(
        json.dumps(document, separators=(",", ":")).encode("utf-8")))
    def normalized_skin(source_document: dict) -> dict:
        source_buffer = cook_assets.source_bytes(Path(temporary), cook_assets.read_gltf(
            json.dumps(source_document, separators=(",", ":")).encode("utf-8")))
        return cook_gltf_geometry.normalized_geometry(source_document, source_buffer)["skin"]

    shared_ancestor = deepcopy(document)
    shared_ancestor["nodes"][5]["translation"] = [5.0, -2.0, 3.0]
    shared_rig = normalized_skin(shared_ancestor)
    if (len(shared_rig["joints"]) != 4 or
            shared_rig["joints"][0]["rest"] != (0.0, 0.0, -0.75, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 1.0)):
        print("glTF skin self-test failed: shared mesh ancestors leaked into the rig", file=sys.stderr)
        return 1

    nested_helper = deepcopy(document)
    nested_helper["nodes"][5]["children"] = [0]
    nested_helper["nodes"][0]["children"] = [4]
    nested_rig = normalized_skin(nested_helper)
    if (len(nested_rig["joints"]) != 3 or
            [joint["parent"] for joint in nested_rig["joints"]] != [-1, 0, 1] or
            [joint for joint in nested_rig["cluster_joints"]] != [1, 2]):
        print("glTF skin self-test failed: descendant helper hierarchy gained a mesh basis",
            file=sys.stderr)
        return 1

    separate_branch = deepcopy(document)
    separate_branch["nodes"][5]["children"] = [0]
    separate_branch["scenes"][0]["nodes"] = [5, 4, 3]
    separate_rig = normalized_skin(separate_branch)
    if (len(separate_rig["joints"]) != 4 or
            separate_rig["joints"][0]["rest"][:3] != (0.0, 0.0, -1.0)):
        print("glTF skin self-test failed: separate skeleton root lacks a mesh-relative basis",
            file=sys.stderr)
        return 1

    matrix_ancestor = deepcopy(document)
    matrix_ancestor["nodes"][4].pop("translation")
    matrix_ancestor["nodes"][4]["matrix"] = [0.0, 1.0, 0.0, 0.0,
        -1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.5, 1.0]
    matrix_rig = normalized_skin(matrix_ancestor)
    matrix_rest = matrix_rig["joints"][1]["rest"]
    if (matrix_rest[:3] != (0.0, 0.0, 0.5) or
            any(abs(value - expected) > 1.0e-6 for value, expected in
                zip(matrix_rest[3:7], (0.0, 0.0, 2.0 ** -0.5, 2.0 ** -0.5)))):
        print("glTF skin self-test failed: matrix ancestor transform was not decomposed",
            file=sys.stderr)
        return 1

    animated_helper = deepcopy(document)
    animated_helper["nodes"][4].pop("translation")
    animated_helper["animations"][0]["channels"].append({"sampler": 0,
        "target": {"node": 4, "path": "translation"}})
    animated_rig = normalized_skin(animated_helper)
    if len(animated_rig["joints"]) != 4 or len(animated_rig["animation_clips"][0]["samples"]) != 4 * 31 * 10:
        print("glTF skin self-test failed: animated helper node was omitted from the rig",
            file=sys.stderr)
        return 1

    rejected = []
    animated = deepcopy(document)
    animated["animations"] = [{"name": "idle"}]
    rejected.append(("animation", animated))
    camera = deepcopy(document)
    camera["cameras"] = [{"type": "perspective"}]
    camera["nodes"][0]["camera"] = 0
    rejected.append(("camera", camera))
    missing_weights = deepcopy(document)
    missing_weights["meshes"][0]["primitives"][0]["attributes"].pop("WEIGHTS_0")
    rejected.append(("missing weights", missing_weights))
    matrix_joint = deepcopy(document)
    matrix_joint["nodes"][1]["matrix"] = [1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 2.0, 0.0, 0.0, 1.0]
    matrix_joint["nodes"][1].pop("translation", None)
    matrix_skin = normalized_skin(matrix_joint)
    if matrix_skin["joints"][2]["rest"][0] != 2.0:
        print("glTF skin self-test failed: matrix joint rest transform was not decomposed",
            file=sys.stderr)
        return 1
    shear = deepcopy(document)
    shear["nodes"][4].pop("translation")
    shear["nodes"][4]["matrix"] = [1.0, 0.0, 0.0, 0.0,
        0.5, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.5, 1.0]
    rejected.append(("sheared skin ancestor matrix", shear))
    duplicate_skin = deepcopy(document)
    duplicate_skin["skins"].append(deepcopy(duplicate_skin["skins"][0]))
    rejected.append(("multiple skins", duplicate_skin))
    singular_inverse_bind = deepcopy(document)
    singular_buffer = bytearray(buffer)
    inverse_accessor = singular_inverse_bind["accessors"][singular_inverse_bind["skins"][0]["inverseBindMatrices"]]
    inverse_view = singular_inverse_bind["bufferViews"][inverse_accessor["bufferView"]]
    struct.pack_into("<f", singular_buffer, inverse_view.get("byteOffset", 0), 0.0)
    singular_inverse_bind["buffers"][0]["uri"] = "data:application/octet-stream;base64," + \
        base64.b64encode(singular_buffer).decode("ascii")
    rejected.append(("singular inverse bind", singular_inverse_bind))
    projective_inverse_bind = deepcopy(document)
    projective_buffer = bytearray(buffer)
    inverse_accessor = projective_inverse_bind["accessors"][projective_inverse_bind["skins"][0]["inverseBindMatrices"]]
    inverse_view = projective_inverse_bind["bufferViews"][inverse_accessor["bufferView"]]
    struct.pack_into("<f", projective_buffer, inverse_view.get("byteOffset", 0) + 3 * 4, 0.25)
    projective_inverse_bind["buffers"][0]["uri"] = "data:application/octet-stream;base64," + \
        base64.b64encode(projective_buffer).decode("ascii")
    rejected.append(("projective inverse bind", projective_inverse_bind))
    cubic = deepcopy(document)
    cubic["animations"][0]["samplers"][0]["interpolation"] = "CUBICSPLINE"
    rejected.append(("cubic-spline animation", cubic))
    morph_channel = deepcopy(document)
    morph_channel["animations"][0]["channels"][0]["target"]["path"] = "weights"
    rejected.append(("morph animation", morph_channel))
    duplicate_track = deepcopy(document)
    duplicate_track["animations"][0]["channels"].append(deepcopy(
        duplicate_track["animations"][0]["channels"][0]))
    rejected.append(("duplicate animation track", duplicate_track))
    for label, unsupported in rejected:
        try:
            unsupported_buffer = cook_assets.source_bytes(Path(temporary), cook_assets.read_gltf(
                json.dumps(unsupported, separators=(",", ":")).encode("utf-8")))
            cook_gltf_geometry.normalized_geometry(unsupported, unsupported_buffer)
        except (ValueError, KeyError, IndexError, TypeError):
            continue
        print(f"glTF skin self-test failed: accepted unsupported {label}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="elisa-gltf-skin-test-") as temporary:
        raise SystemExit(self_test(Path(temporary)))
