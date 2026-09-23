"""Run pinned xatlas to generate deterministic, bounded UV1 lightmap charts."""

from __future__ import annotations

import hashlib
import math
import os
import base64
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
XATLAS_ROOT = ROOT / "dependencies/xatlas"
MAX_VERTICES = 2_000_000
MAX_INDICES = 15_000_000
MIN_RESOLUTION = 16
MAX_RESOLUTION = 8192
MAX_PADDING = 64
INPUT_MAGIC = b"ELISAXA1"
OUTPUT_MAGIC = b"ELISAXR1"
HEADER = struct.Struct("<8sIIII")
OUTPUT_HEADER = struct.Struct("<8sIIIII")
SOURCES = ("native/gltf_xatlas_cooker.cpp", "dependencies/xatlas/xatlas.cpp",
    "dependencies/xatlas/xatlas.h")


def _build_cooker() -> Path:
    paths = [ROOT / source for source in SOURCES]
    if not all(path.is_file() for path in paths):
        raise ValueError("missing pinned xatlas files; run scripts/fetch_dependencies.py")
    compiler = shutil.which(os.environ.get("CXX", "c++"))
    if compiler is None:
        raise ValueError("a C++ compiler is required for the offline xatlas stage")
    version = subprocess.run([compiler, "--version"], capture_output=True, text=True, check=False)
    if version.returncode != 0:
        raise ValueError(version.stderr or version.stdout or "cannot identify the xatlas compiler")
    fingerprint = hashlib.sha256()
    for value in (compiler, version.stdout, os.environ.get("DEVELOPER_DIR", "")):
        fingerprint.update(value.encode("utf-8"))
        fingerprint.update(b"\0")
    for path in paths:
        fingerprint.update(path.read_bytes())
    digest = fingerprint.hexdigest()
    build_dir = ROOT / "build"
    output = build_dir / "gltf-xatlas-cooker"
    stamp = build_dir / "gltf-xatlas-cooker.sha256"
    if output.is_file() and stamp.is_file() and stamp.read_text(encoding="ascii").strip() == digest:
        return output
    build_dir.mkdir(parents=True, exist_ok=True)
    compiled = subprocess.run([compiler, "-std=c++17", "-O2", "-DNDEBUG", "-pthread",
        "-I", str(XATLAS_ROOT), str(paths[0]), str(paths[1]), "-o", str(output)],
        capture_output=True, text=True, check=False)
    if compiled.returncode != 0 or not output.is_file():
        raise ValueError(compiled.stderr or compiled.stdout or "xatlas cooker build failed")
    stamp.write_text(digest + "\n", encoding="ascii")
    return output


def generate(positions: bytes, indices: bytes, vertex_count: int,
        resolution: int = 1024, padding: int = 4) -> dict:
    """Return normalized UV1, a source-vertex map, and a remapped index stream."""
    index_count = len(indices) // 4 if len(indices) % 4 == 0 else 0
    if (type(vertex_count) is not int or not 0 < vertex_count <= MAX_VERTICES or
            len(positions) != vertex_count * 12 or index_count < 3 or index_count > MAX_INDICES or
            index_count % 3 != 0 or type(resolution) is not int or
            not MIN_RESOLUTION <= resolution <= MAX_RESOLUTION or type(padding) is not int or
            not 0 <= padding <= MAX_PADDING):
        raise ValueError("xatlas geometry or packing settings exceed bounded limits")
    if any(not math.isfinite(value) for (value,) in struct.iter_unpack("<f", positions)):
        raise ValueError("xatlas positions contain a non-finite value")
    values = struct.unpack(f"<{index_count}I", indices)
    if any(index >= vertex_count for index in values):
        raise ValueError("xatlas index is out of range")
    packet = HEADER.pack(INPUT_MAGIC, vertex_count, index_count, resolution, padding) + positions + indices
    cooked = subprocess.run([str(_build_cooker())], input=packet, capture_output=True, check=False)
    if cooked.returncode != 0:
        detail = cooked.stderr.decode("utf-8", errors="replace").strip()
        raise ValueError(detail or "xatlas could not build a lightmap atlas")
    response = cooked.stdout
    if len(response) < OUTPUT_HEADER.size:
        raise ValueError("xatlas returned a truncated response")
    magic, output_vertices, output_indices, chart_count, width, height = OUTPUT_HEADER.unpack_from(response)
    expected = OUTPUT_HEADER.size + output_vertices * 12 + output_indices * 4
    if (magic != OUTPUT_MAGIC or not 0 < output_vertices <= MAX_VERTICES or
            output_indices != index_count or chart_count == 0 or not 0 < width <= resolution or
            not 0 < height <= resolution or len(response) != expected):
        raise ValueError("xatlas returned an invalid output layout")
    cursor = OUTPUT_HEADER.size
    uv_bytes = bytearray(output_vertices * 8)
    source_vertices = []
    for vertex in range(output_vertices):
        u, v, source = struct.unpack_from("<2fI", response, cursor)
        cursor += 12
        if (not math.isfinite(u) or not math.isfinite(v) or not 0.0 <= u <= 1.0 or
                not 0.0 <= v <= 1.0 or source >= vertex_count):
            raise ValueError("xatlas returned a non-finite or out-of-range lightmap vertex")
        struct.pack_into("<2f", uv_bytes, vertex * 8, u, v)
        source_vertices.append(source)
    remapped_indices = response[cursor:]
    if any(index >= output_vertices for (index,) in struct.iter_unpack("<I", remapped_indices)):
        raise ValueError("xatlas returned an out-of-range remapped index")
    return {"uv1s": bytes(uv_bytes), "source_vertices": source_vertices,
        "indices": remapped_indices, "chart_count": chart_count,
        "width": width, "height": height}


def self_test() -> int:
    positions = struct.pack("<12f", 0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 1, 0)
    indices = struct.pack("<6I", 0, 1, 2, 1, 3, 2)
    try:
        first = generate(positions, indices, 4, 128, 4)
        repeat = generate(positions, indices, 4, 128, 4)
    except (OSError, RuntimeError, ValueError) as error:
        print(f"glTF lightmap UV self-test failed: {error}", file=sys.stderr)
        return 1
    source_triangles = [tuple(struct.unpack_from("<3f", positions, index * 12) for index in triangle)
        for triangle in ((0, 1, 2), (1, 3, 2))]
    mapped = [tuple(struct.unpack_from("<3f", positions,
        first["source_vertices"][index] * 12) for index in triangle)
        for triangle in struct.iter_unpack("<3I", first["indices"])]
    if (first != repeat or len(first["uv1s"]) != len(first["source_vertices"]) * 8 or
            len(first["source_vertices"]) < 4 or mapped != source_triangles or
            any(not math.isfinite(value) or not 0.0 <= value <= 1.0
                for (value,) in struct.iter_unpack("<f", first["uv1s"]))):
        print("glTF lightmap UV self-test failed: atlas is unstable, invalid, or changed triangle order",
            file=sys.stderr)
        return 1
    for resolution, padding in ((15, 4), (8193, 4), (128, 65)):
        try:
            generate(positions, indices, 4, resolution, padding)
        except ValueError:
            continue
        print("glTF lightmap UV self-test failed: accepted invalid packing settings", file=sys.stderr)
        return 1
    status = cooker_integration_self_test()
    if status != 0:
        return status
    print(f"glTF lightmap UV self-test passed: {len(first['source_vertices'])} vertices, "
        f"{first['chart_count']} charts, deterministic {first['width']}x{first['height']} atlas; "
        "authored, skinned, morphed and simplified streams remap correctly")
    return 0


def cooker_integration_self_test() -> int:
    """Check that xatlas remaps the engine cooker’s full vertex stream set."""
    import cook_assets
    import cook_gltf_geometry
    import cook_gltf_lod
    import gltf_morph_self_test
    import gltf_skin_self_test

    panel_source = ROOT / "test/fixtures/multi_material_panel.gltf"
    panel = cook_assets.read_gltf(panel_source.read_bytes())
    panel_buffer = bytearray(cook_assets.source_bytes(ROOT, panel))
    uv_accessors = {}
    for primitive in panel["meshes"][0]["primitives"]:
        count = panel["accessors"][primitive["attributes"]["POSITION"]]["count"]
        if count not in uv_accessors:
            offset = len(panel_buffer)
            values = (0.25, 0.75) * count
            payload = struct.pack(f"<{count * 2}f", *values)
            panel_buffer += payload
            view_index = len(panel["bufferViews"])
            panel["bufferViews"].append({"buffer": 0, "byteOffset": offset, "byteLength": len(payload)})
            accessor_index = len(panel["accessors"])
            panel["accessors"].append({"bufferView": view_index, "componentType": 5126,
                "count": count, "type": "VEC2"})
            uv_accessors[count] = accessor_index
        primitive["attributes"]["TEXCOORD_1"] = uv_accessors[count]
    panel["buffers"][0]["byteLength"] = len(panel_buffer)
    panel["buffers"][0]["uri"] = "data:application/octet-stream;base64," + \
        base64.b64encode(panel_buffer).decode("ascii")

    fixtures = [
        ("authored", panel, bytes(panel_buffer), False),
        ("generated", cook_assets.read_gltf(panel_source.read_bytes()),
            cook_assets.source_bytes(panel_source.parent, cook_assets.read_gltf(panel_source.read_bytes())), True),
        ("skinned", gltf_skin_self_test.generated_document(), None, True),
        ("morphed", gltf_morph_self_test.generated_document(), None, True),
    ]
    with tempfile.TemporaryDirectory(prefix="elisa-xatlas-integration-") as temporary_name:
        directory = Path(temporary_name)
        for label, document, source_buffer, generate in fixtures:
            buffer = source_buffer if source_buffer is not None else cook_assets.source_bytes(ROOT, document)
            try:
                geometry = cook_gltf_geometry.normalized_geometry(document, buffer,
                    generate_lightmap_uv=generate, lightmap_resolution=128, lightmap_padding=4)
            except (ValueError, KeyError, IndexError, TypeError) as error:
                print(f"glTF lightmap UV self-test failed: {label} geometry: {error}", file=sys.stderr)
                return 1
            vertices = geometry["vertex_count"]
            if len(geometry["uv1s"]) != vertices * 8 or any(
                    not 0.0 <= value <= 1.0 for (value,) in struct.iter_unpack("<f", geometry["uv1s"])):
                print(f"glTF lightmap UV self-test failed: {label} UV1 stream does not cover its vertices",
                    file=sys.stderr)
                return 1
            if label == "authored" and any((u, v) != (0.25, 0.75)
                    for (u, v) in struct.iter_unpack("<2f", geometry["uv1s"])):
                print("glTF lightmap UV self-test failed: authored TEXCOORD_1 was not preserved",
                    file=sys.stderr)
                return 1
            if label == "skinned":
                if (len(geometry["skin_indices"]) != vertices * 4 or
                        len(geometry["skin_weights"]) != vertices * 4 or
                        any(abs(sum(geometry["skin_weights"][offset:offset + 4]) - 1.0) > 1.0e-5
                            for offset in range(0, vertices * 4, 4))):
                    print("glTF lightmap UV self-test failed: xatlas lost skinned vertex influences",
                        file=sys.stderr)
                    return 1
            if label == "morphed" and any(
                    len(target["positions"]) != vertices * 12 or
                    (target["normals"] is not None and len(target["normals"]) != vertices * 12)
                    for target in geometry["morph_targets"]):
                print("glTF lightmap UV self-test failed: xatlas lost morph vertex alignment",
                    file=sys.stderr)
                return 1
        lod_source = cook_gltf_lod.write_test_source(directory)
        document = cook_assets.read_gltf(lod_source.read_bytes())
        geometry = cook_gltf_geometry.normalized_geometry(document,
            cook_assets.source_bytes(lod_source.parent, document), 0.5,
            True, 128, 4)
        if (not geometry["uv1s"] or len(geometry["uv1s"]) != geometry["vertex_count"] * 8 or
                any(len(geometry["uv1s"]) // 8 < placement["vertex_start"] + placement["vertex_count"]
                    for placement in geometry["scene"]["mesh_placements"])):
            print("glTF lightmap UV self-test failed: static LOD stream compaction broke UV1 ranges",
                file=sys.stderr)
            return 1
    return 0


def remap_geometry(geometry: dict, resolution: int = 1024, padding: int = 4) -> dict:
    """Generate one deterministic xatlas chart set per placement and remap all vertex data."""
    import cook_gltf_lightmap_uv

    vertex_count = geometry["vertex_count"]
    if vertex_count <= 0 or len(geometry["positions"]) != vertex_count * 12:
        raise ValueError("lightmap UV generation needs bounded positions")
    placements = geometry["scene"].get("mesh_placements", [])
    if not placements:
        placements = [{"vertex_start": 0, "vertex_count": vertex_count,
            "index_start": 0, "index_count": geometry["index_count"]}]
    strides = {"positions": 12, "normals": 12, "uvs": 8, "tangents": 16, "uv1s": 8}
    streams = {name: geometry.get(name, b"") for name in strides if geometry.get(name, b"")}
    for name, stride in strides.items():
        if name in streams and len(streams[name]) != vertex_count * stride:
            raise ValueError(f"lightmap UV source {name} stream has an invalid vertex stride")
    remapped_streams = {name: bytearray() for name in streams if name != "uv1s"}
    remapped_uv1s = bytearray()
    remapped_indices = bytearray()
    remapped_skin_indices: list[int] = []
    remapped_skin_weights: list[float] = []
    skin_indices = geometry.get("skin_indices", [])
    skin_weights = geometry.get("skin_weights", [])
    if bool(skin_indices) != bool(skin_weights) or (skin_indices and
            (len(skin_indices) != vertex_count * 4 or len(skin_weights) != vertex_count * 4)):
        raise ValueError("lightmap UV source skin streams are incomplete")
    remapped_morphs = [{"positions": bytearray(), "normals": bytearray() if target.get("normals") else None}
        for target in geometry.get("morph_targets", [])]
    if any(len(target["positions"]) != vertex_count * 12 or
            (target.get("normals") is not None and len(target["normals"]) != vertex_count * 12)
            for target in geometry.get("morph_targets", [])):
        raise ValueError("lightmap UV source morph streams have an invalid vertex stride")
    new_placements = []
    total_charts = 0
    for placement in placements:
        vertex_start = placement.get("vertex_start", 0)
        local_vertex_count = placement.get("vertex_count", vertex_count)
        index_start = placement.get("index_start", 0)
        local_index_count = placement.get("index_count", geometry["index_count"])
        if (type(vertex_start) is not int or type(local_vertex_count) is not int or
                type(index_start) is not int or type(local_index_count) is not int or
                vertex_start < 0 or local_vertex_count <= 0 or vertex_start + local_vertex_count > vertex_count or
                index_start < 0 or local_index_count <= 0 or local_index_count % 3 or
                index_start + local_index_count > geometry["index_count"]):
            raise ValueError("lightmap UV placement bounds are invalid")
        source_indices = list(struct.unpack_from(f"<{local_index_count}I", geometry["indices"], index_start * 4))
        if any(index < vertex_start or index >= vertex_start + local_vertex_count for index in source_indices):
            raise ValueError("lightmap UV placement references a foreign vertex")
        local_indices = struct.pack(f"<{local_index_count}I", *(index - vertex_start for index in source_indices))
        local_positions = geometry["positions"][vertex_start * 12:(vertex_start + local_vertex_count) * 12]
        atlas = cook_gltf_lightmap_uv.generate(local_positions, local_indices, local_vertex_count,
            resolution, padding)
        if len(atlas["indices"]) != local_index_count * 4:
            raise ValueError("lightmap UV atlas changed the placement triangle count")
        new_vertex_start = len(remapped_streams["positions"]) // 12
        for name, stream in streams.items():
            if name == "uv1s":
                continue
            stride = strides[name]
            for source in atlas["source_vertices"]:
                offset = (vertex_start + source) * stride
                remapped_streams[name] += stream[offset:offset + stride]
        remapped_uv1s += atlas["uv1s"]
        for index in struct.unpack(f"<{local_index_count}I", atlas["indices"]):
            remapped_indices += struct.pack("<I", new_vertex_start + index)
        if skin_indices:
            for source in atlas["source_vertices"]:
                remapped_skin_indices.extend(skin_indices[(vertex_start + source) * 4:
                    (vertex_start + source + 1) * 4])
                remapped_skin_weights.extend(skin_weights[(vertex_start + source) * 4:
                    (vertex_start + source + 1) * 4])
        for target_index, target in enumerate(geometry.get("morph_targets", [])):
            for source in atlas["source_vertices"]:
                offset = (vertex_start + source) * 12
                remapped_morphs[target_index]["positions"] += target["positions"][offset:offset + 12]
                if target.get("normals") is not None:
                    remapped_morphs[target_index]["normals"] += target["normals"][offset:offset + 12]
        new_placements.append({**placement, "vertex_start": new_vertex_start,
            "vertex_count": len(atlas["source_vertices"])})
        total_charts += atlas["chart_count"]
    if total_charts <= 0 or len(remapped_streams["positions"]) // 12 > MAX_VERTICES:
        raise ValueError("generated lightmap UVs exceed the cooked chart or vertex limits")
    result = {**geometry, **{name: bytes(stream) for name, stream in remapped_streams.items()},
        "uv1s": bytes(remapped_uv1s), "indices": bytes(remapped_indices),
        "vertex_count": len(remapped_uv1s) // 8,
        "scene": {**geometry["scene"], "mesh_placements": new_placements},
        "uv1_metadata": {"source": "xatlas", "resolution": resolution,
            "padding": padding, "chart_count": total_charts}}
    if skin_indices:
        result["skin_indices"] = remapped_skin_indices
        result["skin_weights"] = remapped_skin_weights
    if geometry.get("morph_targets"):
        result["morph_targets"] = [{"positions": bytes(target["positions"]),
            "normals": None if target["normals"] is None else bytes(target["normals"])}
            for target in remapped_morphs]
    return result

if __name__ == "__main__":
    raise SystemExit(self_test())
