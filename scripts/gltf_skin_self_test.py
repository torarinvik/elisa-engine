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
import cook_gltf_geometry


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "test/fixtures/multi_material_panel.gltf"
ASSET_PATH = "test/generated/multi_material_skinned_panel.gltf"


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


def generated_document() -> dict:
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
    identity = (1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        0.0, 0.0, 0.0, 1.0)
    inverse_bind = _append(document, buffer, struct.pack("<32f", *(identity * 2)), 5126, "MAT4", 2)
    input_accessor = _append(document, buffer, struct.pack("<2f", 0.0, 1.0), 5126, "SCALAR", 2)
    output_accessor = _append(document, buffer, struct.pack("<6f", 0.0, 1.0, 0.0, 0.0, 2.0, 0.0),
        5126, "VEC3", 2)
    document["buffers"][0]["byteLength"] = len(buffer)
    document["buffers"][0]["uri"] = "data:application/octet-stream;base64," + base64.b64encode(buffer).decode("ascii")
    document["nodes"][0].update({"children": [1], "skin": 0})
    document["nodes"] += [{"name": "root", "children": [2]},
        {"name": "tip", "translation": [0.0, 1.0, 0.0]}]
    document["skins"] = [{"name": "panel_rig", "joints": [1, 2], "skeleton": 1,
        "inverseBindMatrices": inverse_bind}]
    document["animations"] = [{"name": "lift", "samplers": [{"input": input_accessor,
        "output": output_accessor, "interpolation": "LINEAR"}], "channels": [{"sampler": 0,
        "target": {"node": 2, "path": "translation"}}]}]
    return document


def write_package(output: Path) -> tuple[Path, dict]:
    with tempfile.TemporaryDirectory(prefix="elisa-gltf-skin-") as temporary:
        source = Path(temporary) / "multi_material_skinned_panel.gltf"
        source.write_text(json.dumps(generated_document(), separators=(",", ":")), encoding="utf-8")
        return cook_gltf_geometry.cook_geometry_package(source, ASSET_PATH, output)


def self_test(temporary: Path) -> int:
    first, result = write_package(temporary / "first.pkg")
    second, second_result = write_package(temporary / "second.pkg")
    sections = dict(line.split("=", 1) for line in first.read_text(encoding="utf-8").splitlines())
    required = {
        "format": "elisa-cooked-v3", "material_slots": "2", "subset_count": "3",
        "skin_bones": "2", "skin_joints": "2", "animation_clips": "1", "morph_targets": "1",
    }
    if (result != second_result or first.read_bytes() != second.read_bytes() or
            any(sections.get(key) != value for key, value in required.items()) or
            len(base64.b64decode(sections["skin_indices_b64"])) != 12 * 16 or
            len(base64.b64decode(sections["skin_joint_rest_b64"])) != 2 * 40 or
            sections.get("animation_0_sample_rate") != "30" or
            sections.get("animation_0_frames") != "31"):
        print("glTF skin self-test failed: package is unstable or incomplete", file=sys.stderr)
        return 1
    document = generated_document()
    buffer = cook_assets.source_bytes(Path(temporary), cook_assets.read_gltf(
        json.dumps(document, separators=(",", ":")).encode("utf-8")))
    rejected = []
    animated = deepcopy(document)
    animated["animations"] = [{"name": "idle"}]
    rejected.append(("animation", animated))
    camera = deepcopy(document)
    camera["cameras"] = [{"type": "perspective"}]
    camera["nodes"][0]["camera"] = 0
    rejected.append(("camera", camera))
    transformed = deepcopy(document)
    transformed["nodes"][0]["translation"] = [1.0, 0.0, 0.0]
    rejected.append(("skinned mesh transform", transformed))
    missing_weights = deepcopy(document)
    missing_weights["meshes"][0]["primitives"][0]["attributes"].pop("WEIGHTS_0")
    rejected.append(("missing weights", missing_weights))
    matrix_joint = deepcopy(document)
    matrix_joint["nodes"][1]["matrix"] = [1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0]
    matrix_joint["nodes"][1].pop("translation", None)
    rejected.append(("joint matrix", matrix_joint))
    duplicate_skin = deepcopy(document)
    duplicate_skin["skins"].append(deepcopy(duplicate_skin["skins"][0]))
    rejected.append(("multiple skins", duplicate_skin))
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
            cook_gltf_geometry.normalized_geometry(unsupported, buffer)
        except (ValueError, KeyError, IndexError, TypeError):
            continue
        print(f"glTF skin self-test failed: accepted unsupported {label}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="elisa-gltf-skin-test-") as temporary:
        raise SystemExit(self_test(Path(temporary)))
