"""Prepare an attributed authored rig variant for the animation CPU benchmark.

The pinned Ozz Cesium Man source predates current glTF weight requirements.
Keep the engine's strict importer: repair the benchmark input explicitly and
record hashes and every changed weight range. The benchmark uses a neutral
material through create_mesh, which overrides authored material factors.
"""
from __future__ import annotations

import base64
import copy
import hashlib
import json
import math
from pathlib import Path
import struct

import cook_assets
import cook_gltf_geometry

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "dependencies/ozz/media/gltf/khronos/cesium_man.gltf"
SOURCE_SHA256 = "e97bf032c41e1199772051f591b0a16cd1de046a1c9da119ceb13538a057c0fb"
OUTPUT = ROOT / "build/authored-animation/cesium-man.pkg"


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def prepare() -> dict:
    if digest(SOURCE) != SOURCE_SHA256:
        raise ValueError("authored source differs from the pinned benchmark fixture")
    original = cook_assets.read_gltf(SOURCE.read_bytes())
    if len(original["skins"]) != 1 or len(original["skins"][0]["joints"]) != 19 or len(original["animations"]) != 1:
        raise ValueError("expected Cesium Man's 19 skin joints and one animation")
    document = copy.deepcopy(original)
    document["materials"] = [{"name": "benchmark-neutral", "pbrMetallicRoughness": {
        "baseColorFactor": [0.8, 0.9, 1.0, 1.0], "metallicFactor": 0.0, "roughnessFactor": 0.7}}]
    for key in ("textures", "images", "samplers"):
        document.pop(key, None)
    buffer = bytearray(cook_assets.source_bytes(SOURCE.parent, original))
    attributes = document["meshes"][0]["primitives"][0]["attributes"]
    accessor = document["accessors"][attributes["WEIGHTS_0"]]
    view = document["bufferViews"][accessor["bufferView"]]
    start = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
    stride = view.get("byteStride", 16)
    if accessor["componentType"] != 5126 or accessor["type"] != "VEC4":
        raise ValueError("expected the pinned float32 weight layout")
    # Prove the ordinary importer refuses the unnormalized original influences.
    try:
        cook_gltf_geometry.normalized_geometry(document, bytes(buffer))
    except ValueError as error:
        if str(error) != "skin weights must be finite, nonnegative, and normalized":
            raise
    else:
        raise ValueError("negative control: invalid source weights were accepted")
    repairs = []
    for vertex in range(accessor["count"]):
        offset = start + vertex * stride
        values = struct.unpack_from("<4f", buffer, offset)
        total = sum(values)
        if not all(math.isfinite(value) and value >= 0 for value in values) or total <= 0:
            raise ValueError("benchmark cannot repair non-finite, negative or empty influences")
        if abs(total - 1.0) > 1e-6:
            normalized = [value / total for value in values]
            struct.pack_into("<4f", buffer, offset, *normalized)
            repairs.append({"vertex": vertex, "source_sum": total})
    if len(repairs) != 210:
        raise ValueError("unexpected source weight repair count")
    # Preserve every buffer byte except the recorded influence ranges.
    source_buffer = cook_assets.source_bytes(SOURCE.parent, original)
    repaired_offsets = {start + repair["vertex"] * stride + byte for repair in repairs for byte in range(16)}
    if any(a != b and offset not in repaired_offsets for offset, (a, b) in enumerate(zip(source_buffer, buffer))):
        raise ValueError("repair modified geometry or animation bytes")
    for key in ("nodes", "skins", "animations", "meshes", "accessors", "bufferViews"):
        if document[key] != original[key]:
            raise ValueError("benchmark changed the authored hierarchy, motion or geometry")
    document["buffers"][0]["uri"] = "data:application/octet-stream;base64," + base64.b64encode(buffer).decode("ascii")
    derived = OUTPUT.with_suffix(".gltf")
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    derived.write_text(json.dumps(document, sort_keys=True, separators=(",", ":")), encoding="utf-8")
    _, counts = cook_gltf_geometry.cook_geometry_package(derived, "cesium-man", OUTPUT,
        animation_contract_path=OUTPUT.with_suffix(".anim"))
    joints, clips = struct.unpack_from("<II", OUTPUT.with_suffix(".anim").read_bytes(), 20)
    if joints != 20 or clips != 1:
        raise ValueError("cooked authored rig counts differ")
    report = {
        "source": str(SOURCE.relative_to(ROOT)), "source_sha256": SOURCE_SHA256,
        "attribution": "Cesium Man, donated by Cesium to Khronos glTF Sample Models; CC BY 4.0. Derived benchmark: normalized 210 vertex weight sums; neutral material replaces texture.",
        "license_evidence": "dependencies/ozz/media/gltf/khronos/README.md",
        "source_weight_sum_min": min(repair["source_sum"] for repair in repairs),
        "weight_repairs": repairs, "preserved": "geometry, hierarchy and animation bytes outside recorded weight ranges",
        "derived_source_sha256": digest(derived), "package_sha256": digest(OUTPUT),
        "contract_sha256": digest(OUTPUT.with_suffix(".anim")),
        "joints": joints, "clips": clips, "clip_name": "animation_0", "counts": counts,
        "runtime_asset": str(OUTPUT.relative_to(ROOT)),
    }
    OUTPUT.with_suffix(".provenance.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report


if __name__ == "__main__":
    result = prepare()
    print(f"authored fixture: joints={result['joints']} clips={result['clips']} repaired_weights=210 asset={result['runtime_asset']}")
