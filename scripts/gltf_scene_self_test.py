"""Generate and validate bounded glTF camera and punctual-light metadata."""

from __future__ import annotations

from copy import deepcopy
import json
from pathlib import Path
import sys
import tempfile

import cook_assets
import cook_gltf_geometry


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "test/fixtures/multi_material_panel.gltf"
ASSET_PATH = "test/generated/scene_metadata_panel.gltf"


def generated_document() -> dict:
    document = cook_assets.read_gltf(SOURCE.read_bytes())
    document["cameras"] = [
        {"name": "main", "type": "perspective", "perspective":
            {"yfov": 1.0, "znear": 0.1, "zfar": 100.0, "aspectRatio": 1.5}},
        {"name": "top", "type": "orthographic", "orthographic":
            {"xmag": 5.0, "ymag": 4.0, "znear": 0.1, "zfar": 50.0}},
    ]
    document["extensionsUsed"] = ["KHR_lights_punctual"]
    document["extensions"] = {"KHR_lights_punctual": {"lights": [
        {"name": "sun", "type": "directional", "color": [1.0, 0.9, 0.8], "intensity": 2.0},
        {"name": "spot", "type": "spot", "color": [0.5, 0.7, 1.0], "intensity": 4.0,
            "range": 20.0, "spot": {"innerConeAngle": 0.2, "outerConeAngle": 0.6}},
    ]}}
    document["nodes"][0].update({"camera": 0, "extensions":
        {"KHR_lights_punctual": {"light": 0}}, "children": [1]})
    document["nodes"].append({"name": "top_view", "camera": 1, "translation": [0.0, 5.0, 0.0],
        "extensions": {"KHR_lights_punctual": {"light": 1}}})
    return document


def write_package(output: Path) -> tuple[Path, dict]:
    with tempfile.TemporaryDirectory(prefix="elisa-gltf-scene-") as temporary:
        source = Path(temporary) / "scene_metadata_panel.gltf"
        source.write_text(json.dumps(generated_document(), separators=(",", ":")), encoding="utf-8")
        return cook_gltf_geometry.cook_geometry_package(source, ASSET_PATH, output)


def self_test(temporary: Path) -> int:
    first, result = write_package(temporary / "first.pkg")
    second, second_result = write_package(temporary / "second.pkg")
    fields = dict(line.split("=", 1) for line in first.read_text(encoding="utf-8").splitlines())
    if (result != second_result or first.read_bytes() != second.read_bytes() or result["cameras"] != 2 or
            result["lights"] != 2 or fields.get("camera_count") != "2" or fields.get("light_count") != "2"):
        print("glTF scene self-test failed: camera/light package is unstable or incomplete", file=sys.stderr)
        return 1
    document = generated_document()
    buffer = cook_assets.source_bytes(Path(temporary), cook_assets.read_gltf(
        json.dumps(document, separators=(",", ":")).encode("utf-8")))
    rejected = []
    bad_camera = deepcopy(document)
    bad_camera["cameras"][0]["perspective"]["zfar"] = 0.05
    rejected.append(("camera clip order", bad_camera))
    bad_light = deepcopy(document)
    bad_light["extensions"]["KHR_lights_punctual"]["lights"][1]["spot"]["outerConeAngle"] = 2.0
    rejected.append(("spot cone", bad_light))
    missing_extension = deepcopy(document)
    missing_extension["extensionsUsed"] = []
    rejected.append(("missing light extension", missing_extension))
    bad_node = deepcopy(document)
    bad_node["nodes"][1]["camera"] = 9
    rejected.append(("camera reference", bad_node))
    unreferenced_camera = deepcopy(document)
    unreferenced_camera["cameras"].append({"type": "perspective", "perspective":
        {"yfov": 1.0, "znear": 10.0, "zfar": 1.0}})
    rejected.append(("unreferenced camera metadata", unreferenced_camera))
    unreferenced_light = deepcopy(document)
    unreferenced_light["extensions"]["KHR_lights_punctual"]["lights"].append(
        {"type": "point", "range": -1.0})
    rejected.append(("unreferenced light metadata", unreferenced_light))
    for label, variant in rejected:
        try:
            cook_gltf_geometry.normalized_geometry(variant, buffer)
        except (ValueError, KeyError, IndexError, TypeError):
            continue
        print(f"glTF scene self-test failed: accepted {label}", file=sys.stderr)
        return 1
    print("glTF scene self-test passed: deterministic camera/light metadata package")
    return 0


if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="elisa-gltf-scene-test-") as temporary:
        raise SystemExit(self_test(Path(temporary)))
