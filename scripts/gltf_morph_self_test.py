"""Generate and validate the bounded glTF morph-target fixture."""

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
from cooked_meshopt_test_stream import decode_vertex_stream


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "test/fixtures/multi_material_panel.gltf"
ASSET_PATH = "test/generated/multi_material_morphed_panel.gltf"


def _append(document: dict, buffer: bytearray, payload: bytes, count: int,
        value_type: str = "VEC3", component: int = 5126,
        normalized: bool = False) -> int:
    offset = len(buffer)
    if offset % 4:
        buffer += bytes(4 - offset % 4)
        offset = len(buffer)
    buffer += payload
    view = len(document["bufferViews"])
    document["bufferViews"].append({"buffer": 0, "byteOffset": offset, "byteLength": len(payload)})
    accessor = {"bufferView": view, "componentType": component,
        "count": count, "type": value_type}
    if normalized:
        accessor["normalized"] = True
    document["accessors"].append(accessor)
    return len(document["accessors"]) - 1


def generated_document() -> dict:
    document = cook_assets.read_gltf(SOURCE.read_bytes())
    buffer = bytearray(cook_assets.source_bytes(SOURCE.parent, document))
    target_accessors = {}
    normal_accessors = {}
    for primitive in document["meshes"][0]["primitives"]:
        count = document["accessors"][primitive["attributes"]["POSITION"]]["count"]
        if count not in target_accessors:
            deltas = (0.0, 0.0, 0.25) * count
            target_accessors[count] = _append(document, buffer, struct.pack(f"<{count * 3}f", *deltas), count)
            normals = (0.0, 0.0, 0.0) * count
            normal_accessors[count] = _append(document, buffer, struct.pack(f"<{count * 3}f", *normals), count)
        primitive["targets"] = [{"POSITION": target_accessors[count], "NORMAL": normal_accessors[count]}]
    document["buffers"][0]["byteLength"] = len(buffer)
    document["buffers"][0]["uri"] = "data:application/octet-stream;base64," + base64.b64encode(buffer).decode("ascii")
    return document


def animated_document(second_animation: bool = False) -> dict:
    """Animate one unskinned placement while a sibling keeps mesh defaults."""
    document = generated_document()
    buffer = bytearray(base64.b64decode(document["buffers"][0]["uri"].split(",", 1)[1]))
    input_accessor = _append(document, buffer, struct.pack("<2f", 0.0, 1.0), 2, "SCALAR")
    document["accessors"][input_accessor].update({"min": [0.0], "max": [1.0]})
    output_accessor = _append(document, buffer, struct.pack("<2f", 0.0, 1.0), 2, "SCALAR")
    document["meshes"][0]["weights"] = [0.2]
    document["nodes"][0]["weights"] = [0.4]
    second = deepcopy(document["nodes"][0])
    second["name"] = "default_weight_placement"
    second.pop("weights")
    second["translation"] = [2.0, 0.0, 0.0]
    second_node = len(document["nodes"])
    document["nodes"].append(second)
    document["scenes"][0]["nodes"].append(second_node)
    document["animations"] = [{"name": "weights", "samplers": [{"input": input_accessor,
        "output": output_accessor, "interpolation": "LINEAR"}], "channels": [{"sampler": 0,
        "target": {"node": 0, "path": "weights"}}]}]
    if second_animation:
        return_output = _append(document, buffer, struct.pack("<2f", 1.0, 0.0), 2, "SCALAR")
        document["animations"].append({"name": "weights-return", "samplers": [{
            "input": input_accessor, "output": return_output, "interpolation": "LINEAR"}],
            "channels": [{"sampler": 0, "target": {"node": 0, "path": "weights"}}]})
    document["buffers"][0]["byteLength"] = len(buffer)
    document["buffers"][0]["uri"] = "data:application/octet-stream;base64," + base64.b64encode(buffer).decode("ascii")
    return document


def animation_output_document(payload: bytes, component: int = 5126,
        normalized: bool = False, interpolation: str = "LINEAR") -> dict:
    """Replace the scalar weight output with a selected glTF encoding."""
    document = animated_document()
    buffer = bytearray(base64.b64decode(document["buffers"][0]["uri"].split(",", 1)[1]))
    output = _append(document, buffer, payload, 2 if interpolation != "CUBICSPLINE" else 6,
        "SCALAR", component, normalized)
    document["animations"][0]["samplers"][0].update({
        "output": output, "interpolation": interpolation})
    document["buffers"][0]["byteLength"] = len(buffer)
    document["buffers"][0]["uri"] = "data:application/octet-stream;base64," + \
        base64.b64encode(buffer).decode("ascii")
    return document


def write_package(output: Path) -> tuple[Path, dict]:
    with tempfile.TemporaryDirectory(prefix="elisa-gltf-morph-") as temporary:
        source = Path(temporary) / "multi_material_morphed_panel.gltf"
        source.write_text(json.dumps(generated_document(), separators=(",", ":")), encoding="utf-8")
        return cook_gltf_geometry.cook_geometry_package(source, ASSET_PATH, output)


def write_animated_package(output: Path,
        second_animation: bool = False) -> tuple[Path, dict]:
    with tempfile.TemporaryDirectory(prefix="elisa-gltf-morph-animation-") as temporary:
        source = Path(temporary) / "morph_animation_panel.gltf"
        source.write_text(json.dumps(animated_document(second_animation), separators=(",", ":")),
            encoding="utf-8")
        return cook_gltf_geometry.cook_geometry_package(source, ASSET_PATH, output)


def self_test(temporary: Path) -> int:
    first, result = write_package(temporary / "first.pkg")
    second, second_result = write_package(temporary / "second.pkg")
    sections = dict(line.split("=", 1) for line in first.read_text(encoding="utf-8").splitlines())
    position_encodings = sum(name in sections for name in
        ("morph_0_positions_b64", "morph_0_positions_meshopt_b64"))
    normal_encodings = sum(name in sections for name in
        ("morph_0_normals_b64", "morph_0_normals_meshopt_b64"))
    morph_positions = decode_vertex_stream(sections, "morph_0_positions", 12, 12)
    morph_normals = decode_vertex_stream(sections, "morph_0_normals", 12, 12)
    if (result != second_result or first.read_bytes() != second.read_bytes() or
            result["positions"] != 12 or result["morph_targets"] != 1 or
            sections.get("morph_targets") != "1" or sections.get("morph_target_position_stride") != "12" or
            sections.get("morph_target_normal_stride") != "12" or
            position_encodings != 1 or normal_encodings != 1 or
            "morph_0_positions_meshopt_b64" not in sections or
            "morph_0_normals_meshopt_b64" not in sections):
        print("glTF morph self-test failed: package is unstable or incomplete", file=sys.stderr)
        return 1
    if len(morph_positions) != 12 * 12 or len(morph_normals) != 12 * 12:
        print("glTF morph self-test failed: morph streams changed while encoding", file=sys.stderr)
        return 1
    animated_path, _ = write_animated_package(temporary / "animated.pkg")
    animated = dict(line.split("=", 1) for line in animated_path.read_text(encoding="utf-8").splitlines())
    morph_samples = struct.unpack("<62f", base64.b64decode(animated["animation_0_morph_samples_b64"]))
    defaults = struct.unpack("<2f", base64.b64decode(animated["morph_default_weights_b64"]))
    if (animated.get("format") != "elisa-cooked-v3" or animated.get("animation_clips") != "1" or
            "skin_joints" in animated or any(abs(value - expected) > 1.0e-6
                for value, expected in zip(defaults, (0.4, 0.2))) or
            any(abs(value - expected) > 1.0e-6 for value, expected in
                zip(morph_samples[0:2], (0.0, 0.2))) or
            any(abs(value - expected) > 1.0e-6 for value, expected in
                zip(morph_samples[30:32], (0.5, 0.2))) or
            any(abs(value - expected) > 1.0e-6 for value, expected in
                zip(morph_samples[60:62], (1.0, 0.2)))):
        print("glTF morph self-test failed: unskinned weight clips or placement defaults were lost",
            file=sys.stderr)
        return 1

    quantized_document = animation_output_document(bytes((0, 255)), 5121, True)
    quantized_buffer = cook_assets.source_bytes(Path(temporary), cook_assets.read_gltf(
        json.dumps(quantized_document, separators=(",", ":")).encode("utf-8")))
    quantized_geometry = cook_gltf_geometry.normalized_geometry(quantized_document, quantized_buffer)
    quantized_samples = quantized_geometry["animation_clips"][0]["morph_weights"]
    if (abs(quantized_samples[30] - 0.5) > 1.0e-6 or
            abs(quantized_samples[60] - 1.0) > 1.0e-6):
        print("glTF morph self-test failed: normalized integer weights were not decoded",
            file=sys.stderr)
        return 1

    cubic_document = animation_output_document(struct.pack("<6f", 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0), interpolation="CUBICSPLINE")
    cubic_buffer = cook_assets.source_bytes(Path(temporary), cook_assets.read_gltf(
        json.dumps(cubic_document, separators=(",", ":")).encode("utf-8")))
    cubic_geometry = cook_gltf_geometry.normalized_geometry(cubic_document, cubic_buffer)
    cubic_samples = cubic_geometry["animation_clips"][0]["morph_weights"]
    if (abs(cubic_samples[30] - 0.5) > 1.0e-6 or
            abs(cubic_samples[60] - 1.0) > 1.0e-6 or
            abs(cubic_samples[61] - 0.2) > 1.0e-6):
        print("glTF morph self-test failed: cubic-spline weights or sibling defaults were lost",
            file=sys.stderr)
        return 1
    document = generated_document()
    buffer = cook_assets.source_bytes(Path(temporary), cook_assets.read_gltf(
        json.dumps(document, separators=(",", ":")).encode("utf-8")))
    rejected = []
    missing = deepcopy(document)
    missing["meshes"][0]["primitives"][0]["targets"][0].pop("POSITION")
    rejected.append(("missing morph position", missing))
    inconsistent = deepcopy(document)
    inconsistent["meshes"][0]["primitives"][1].pop("targets")
    rejected.append(("inconsistent morph count", inconsistent))
    unknown = deepcopy(document)
    unknown["meshes"][0]["primitives"][0]["targets"][0]["TANGENT"] = 1
    rejected.append(("morph tangent", unknown))
    too_many = deepcopy(document)
    too_many["meshes"][0]["primitives"][0]["targets"] = too_many["meshes"][0]["primitives"][0]["targets"] * 33
    rejected.append(("too many morph targets", too_many))
    for label, variant in rejected:
        try:
            cook_gltf_geometry.normalized_geometry(variant, buffer)
        except (ValueError, KeyError, IndexError, TypeError):
            continue
        print(f"glTF morph self-test failed: accepted {label}", file=sys.stderr)
        return 1
    print("glTF morph self-test passed: deterministic one-target panel package")
    return 0


if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="elisa-gltf-morph-test-") as temporary:
        raise SystemExit(self_test(Path(temporary)))
