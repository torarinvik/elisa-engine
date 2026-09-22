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


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "test/fixtures/multi_material_panel.gltf"
ASSET_PATH = "test/generated/multi_material_morphed_panel.gltf"


def _append(document: dict, buffer: bytearray, payload: bytes, count: int) -> int:
    offset = len(buffer)
    if offset % 4:
        buffer += bytes(4 - offset % 4)
        offset = len(buffer)
    buffer += payload
    view = len(document["bufferViews"])
    document["bufferViews"].append({"buffer": 0, "byteOffset": offset, "byteLength": len(payload)})
    document["accessors"].append({"bufferView": view, "componentType": 5126,
        "count": count, "type": "VEC3"})
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


def write_package(output: Path) -> tuple[Path, dict]:
    with tempfile.TemporaryDirectory(prefix="elisa-gltf-morph-") as temporary:
        source = Path(temporary) / "multi_material_morphed_panel.gltf"
        source.write_text(json.dumps(generated_document(), separators=(",", ":")), encoding="utf-8")
        return cook_gltf_geometry.cook_geometry_package(source, ASSET_PATH, output)


def self_test(temporary: Path) -> int:
    first, result = write_package(temporary / "first.pkg")
    second, second_result = write_package(temporary / "second.pkg")
    sections = dict(line.split("=", 1) for line in first.read_text(encoding="utf-8").splitlines())
    if (result != second_result or first.read_bytes() != second.read_bytes() or
            result["positions"] != 12 or result["morph_targets"] != 1 or
            sections.get("morph_targets") != "1" or sections.get("morph_target_position_stride") != "12" or
            sections.get("morph_target_normal_stride") != "12" or
            len(base64.b64decode(sections.get("morph_0_positions_b64", ""))) != 12 * 12 or
            len(base64.b64decode(sections.get("morph_0_normals_b64", ""))) != 12 * 12):
        print("glTF morph self-test failed: package is unstable or incomplete", file=sys.stderr)
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
