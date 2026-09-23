"""Create bounded static glTF LOD variants with per-placement material boundaries."""

from __future__ import annotations

import base64
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import struct
import subprocess


ROOT = Path(__file__).resolve().parents[1]
MESHOPT = ROOT / "dependencies/meshoptimizer"
INPUT_MAGIC = b"ELISALD1"
OUTPUT_MAGIC = b"ELISALR1"
HEADER = struct.Struct("<8sIIIff")
RANGE = struct.Struct("<III")
OUTPUT_HEADER = struct.Struct("<8sI")
OUTPUT_RANGE = struct.Struct("<If")
INDEX = struct.Struct("<I")
MAX_VERTICES = 2_000_000
MAX_INDICES = 15_000_000
MAX_RANGES = 16 * 256
MAX_LOD_ERROR = 0.02
SOURCES = (
    "native/meshopt_lod_cooker.cpp",
    "dependencies/meshoptimizer/meshoptimizer.h",
    "dependencies/meshoptimizer/simplifier.cpp",
    "dependencies/meshoptimizer/allocator.cpp",
)


def _build_cooker() -> Path:
    paths = [ROOT / source for source in SOURCES]
    if not all(path.is_file() for path in paths):
        raise ValueError("missing pinned meshoptimizer files; run scripts/fetch_dependencies.py")
    compiler = os.environ.get("CXX", "c++")
    compiler_path = shutil.which(compiler)
    if compiler_path is None:
        raise ValueError(f"C++ compiler is unavailable: {compiler}")
    version = subprocess.run([compiler_path, "--version"], capture_output=True,
        text=True, check=False)
    if version.returncode != 0:
        raise ValueError(version.stderr or version.stdout or "cannot identify C++ compiler")
    fingerprint = hashlib.sha256(compiler_path.encode("utf-8") + version.stdout.encode("utf-8"))
    fingerprint.update(os.environ.get("DEVELOPER_DIR", "").encode("utf-8"))
    for path in paths:
        fingerprint.update(path.read_bytes())
    digest = fingerprint.hexdigest()
    output = ROOT / "build/meshopt-lod-cooker"
    stamp = ROOT / "build/meshopt-lod-cooker.sha256"
    if output.is_file() and stamp.is_file() and stamp.read_text(encoding="ascii").strip() == digest:
        return output
    output.parent.mkdir(parents=True, exist_ok=True)
    command = [compiler_path, "-std=c++17", "-O2", "-DNDEBUG", "-I", str(MESHOPT),
        str(paths[0]), str(paths[2]), str(paths[3]), "-o", str(output)]
    built = subprocess.run(command, capture_output=True, text=True, check=False)
    if built.returncode != 0 or not output.is_file():
        raise ValueError(built.stderr or built.stdout or "meshoptimizer LOD cooker build failed")
    stamp.write_text(digest + "\n", encoding="ascii")
    return output


def _placement_chunks(geometry: dict) -> list[tuple[int, int, int, int]]:
    indices = geometry["indices"]
    index_count = len(indices) // INDEX.size
    subsets = geometry["subsets"]
    placements = geometry["scene"]["mesh_placements"]
    if (not indices or len(indices) % INDEX.size or index_count % 3 or not subsets or
            not placements or len(subsets) > 16 or len(placements) > 256):
        raise ValueError("LOD input has invalid bounded triangle, subset, or placement counts")
    covered = 0
    for start, count, material_slot in subsets:
        if (start != covered or count <= 0 or count % 3 or count > index_count - covered or
                not 0 <= material_slot < 16):
            raise ValueError("LOD material subsets must partition the triangle index stream")
        covered += count
    if covered != index_count:
        raise ValueError("LOD material subsets do not cover the index stream")
    chunks = []
    covered_placements = 0
    for placement_index, placement in enumerate(placements):
        start = placement["index_start"]
        count = placement["index_count"]
        if (start != covered_placements or count <= 0 or count % 3 or
                count > index_count - covered_placements):
            raise ValueError("LOD placement index ranges must partition the triangle stream")
        end = start + count
        overlap_total = 0
        for subset_start, subset_count, material_slot in subsets:
            overlap_start = max(start, subset_start)
            overlap_end = min(end, subset_start + subset_count)
            if overlap_start < overlap_end:
                overlap_count = overlap_end - overlap_start
                if (overlap_start % 3 or overlap_count % 3 or
                        (chunks and chunks[-1][0] == placement_index and
                            chunks[-1][1] + chunks[-1][2] != overlap_start)):
                    raise ValueError("LOD placement/material ranges split or gap a triangle range")
                chunks.append((placement_index, overlap_start, overlap_count, material_slot))
                overlap_total += overlap_count
        if overlap_total != count:
            raise ValueError("LOD material ranges do not cover a mesh placement")
        covered_placements += count
    if covered_placements != index_count or not chunks or len(chunks) > MAX_RANGES:
        raise ValueError("LOD placement ranges do not cover the bounded index stream")
    return chunks


def _compact_placement_vertices(geometry: dict, placements: list[dict],
        indices: bytes) -> tuple[dict[str, bytes], bytes]:
    """Compact each placement independently and remap all static vertex streams."""
    source_vertex_count = geometry["vertex_count"]
    source_placements = geometry["scene"]["mesh_placements"]
    if len(placements) != len(source_placements):
        raise ValueError("LOD placement metadata changed during vertex compaction")
    strides = {"positions": 12, "normals": 12, "uvs": 8, "uv1s": 8, "tangents": 16}
    source_streams = {}
    for name, stride in strides.items():
        stream = geometry.get(name, b"")
        if name == "positions" or stream:
            if len(stream) != source_vertex_count * stride:
                raise ValueError(f"LOD {name} stream has an invalid vertex stride")
            source_streams[name] = stream
    compacted = {name: bytearray() for name in source_streams}
    remapped_indices = bytearray(indices)
    total_vertices = 0
    source_vertex_ranges = []
    for source, placement in zip(source_placements, placements):
        vertex_start = source.get("vertex_start", 0)
        vertex_count = source.get("vertex_count", source_vertex_count)
        index_start = placement["index_start"]
        index_count = placement["index_count"]
        if (type(vertex_start) is not int or type(vertex_count) is not int or
                vertex_start < 0 or vertex_count <= 0 or vertex_start > source_vertex_count or
                vertex_count > source_vertex_count - vertex_start or index_count <= 0 or
                index_start < 0 or index_count > len(indices) // INDEX.size - index_start):
            raise ValueError("LOD placement has invalid vertex or index ranges")
        if any(vertex_start < end and start < vertex_start + vertex_count
                for start, end in source_vertex_ranges):
            raise ValueError("LOD source placements share vertex ranges")
        source_vertex_ranges.append((vertex_start, vertex_start + vertex_count))
        index_offset = index_start * INDEX.size
        index_bytes = indices[index_offset:index_offset + index_count * INDEX.size]
        used_vertices = sorted({index for (index,) in
            struct.iter_unpack("<I", index_bytes)})
        if any(index < vertex_start or index >= vertex_start + vertex_count
                for index in used_vertices):
            raise ValueError("LOD placement references a vertex outside its source range")
        new_start = total_vertices
        remap = {}
        for old_index in used_vertices:
            remap[old_index] = total_vertices
            for name, stream in source_streams.items():
                stride = strides[name]
                byte_start = old_index * stride
                compacted[name] += stream[byte_start:byte_start + stride]
            total_vertices += 1
        for offset in range(index_offset, index_offset + index_count * INDEX.size, INDEX.size):
            old_index = INDEX.unpack_from(remapped_indices, offset)[0]
            struct.pack_into("<I", remapped_indices, offset, remap[old_index])
        placement["vertex_start"] = new_start
        placement["vertex_count"] = len(used_vertices)
    if total_vertices == 0 or total_vertices > source_vertex_count:
        raise ValueError("LOD vertex compaction produced an invalid vertex total")
    return {name: bytes(stream) for name, stream in compacted.items()}, bytes(remapped_indices)


def simplify_geometry(geometry: dict, ratio: float) -> tuple[dict, dict]:
    """Simplify each placement/material intersection without changing vertex streams."""
    if type(ratio) not in (int, float) or not math.isfinite(ratio) or not 0.0 < ratio < 1.0:
        raise ValueError("LOD simplification ratio must be finite and between 0 and 1")
    if (geometry.get("skin") is not None or geometry.get("animation_clips") or
            geometry.get("morph_targets")):
        raise ValueError("LOD simplification currently supports static meshes without skin or morph animation")
    positions = geometry["positions"]
    indices = geometry["indices"]
    vertex_count = geometry["vertex_count"]
    source_attribute_bytes = sum(len(geometry.get(name, b""))
        for name in ("positions", "normals", "uvs", "uv1s", "tangents"))
    chunks = _placement_chunks(geometry)
    index_count = len(indices) // INDEX.size
    if (vertex_count <= 0 or vertex_count > MAX_VERTICES or
            len(positions) != vertex_count * 12 or index_count > MAX_INDICES):
        raise ValueError("LOD geometry exceeds the bounded position or index limits")
    targets = []
    source_triangles = 0
    target_triangles = 0
    for placement, start, count, material_slot in chunks:
        triangles = count // 3
        target = max(1, math.floor(triangles * ratio)) * 3
        targets.append((placement, start, count, material_slot, target))
        source_triangles += triangles
        target_triangles += target // 3
    packet = bytearray(HEADER.pack(INPUT_MAGIC, vertex_count, index_count, len(chunks),
        float(ratio), MAX_LOD_ERROR))
    packet += b"".join(RANGE.pack(start, count, target) for _, start, count, _, target in targets)
    packet += positions
    packet += indices
    optimized = subprocess.run([str(_build_cooker())], input=packet, capture_output=True, check=False)
    if optimized.returncode != 0:
        detail = optimized.stderr.decode("utf-8", errors="replace").strip()
        raise ValueError(detail or "meshoptimizer could not create the requested LOD")
    response = optimized.stdout
    if len(response) < OUTPUT_HEADER.size or OUTPUT_HEADER.unpack_from(response) != (OUTPUT_MAGIC, len(chunks)):
        raise ValueError("meshoptimizer returned an invalid LOD response header")
    offset = OUTPUT_HEADER.size
    simplified_chunks = []
    output_indices = bytearray()
    actual_triangles = 0
    maximum_error = 0.0
    for placement, _, _, material_slot, target_count in targets:
        if offset + OUTPUT_RANGE.size > len(response):
            raise ValueError("meshoptimizer returned a truncated LOD range")
        count, error = OUTPUT_RANGE.unpack_from(response, offset)
        offset += OUTPUT_RANGE.size
        if (count < 3 or count > target_count or count % 3 or not math.isfinite(error) or
                error < 0.0 or error > MAX_LOD_ERROR or
                count > (len(response) - offset) // INDEX.size):
            raise ValueError("meshoptimizer returned an invalid LOD triangle range")
        index_bytes = response[offset:offset + count * INDEX.size]
        offset += count * INDEX.size
        if any(index >= vertex_count for (index,) in struct.iter_unpack("<I", index_bytes)):
            raise ValueError("meshoptimizer returned an out-of-range LOD vertex index")
        simplified_chunks.append((placement, index_bytes, material_slot))
        output_indices += index_bytes
        actual_triangles += count // 3
        maximum_error = max(maximum_error, error)
    if offset != len(response) or actual_triangles > target_triangles:
        raise ValueError("meshoptimizer returned trailing data or exceeded the aggregate triangle target")
    if actual_triangles >= source_triangles:
        raise ValueError("no placement/material range can be simplified within the configured error limit")

    subsets = []
    placement_records = [dict(record, index_start=0, index_count=0) for record in geometry["scene"]["mesh_placements"]]
    index_cursor = 0
    active_placement = -1
    for placement, index_bytes, material_slot in simplified_chunks:
        count = len(index_bytes) // INDEX.size
        if placement != active_placement:
            active_placement = placement
            placement_records[placement]["index_start"] = index_cursor
        if subsets and subsets[-1][2] == material_slot:
            start, previous_count, slot = subsets[-1]
            subsets[-1] = (start, previous_count + count, slot)
        else:
            subsets.append((index_cursor, count, material_slot))
        placement_records[placement]["index_count"] += count
        index_cursor += count
    for placement in placement_records:
        start = placement["index_start"]
        end = start + placement["index_count"]
        overlaps = [(index, subset) for index, subset in enumerate(subsets)
            if subset[0] < end and subset[0] + subset[1] > start]
        if not overlaps or sum(min(end, row[0] + row[1]) - max(start, row[0]) for _, row in overlaps) != end - start:
            raise ValueError("simplified LOD no longer partitions its placement subsets")
        placement["subset_start"] = overlaps[0][0]
        placement["subset_count"] = len(overlaps)
    if len(subsets) > 16:
        raise ValueError("simplified LOD exceeds the runtime material-subset limit")
    compacted_streams, remapped_indices = _compact_placement_vertices(
        geometry, placement_records, bytes(output_indices))
    geometry.update(compacted_streams)
    geometry["indices"] = remapped_indices
    geometry["vertex_count"] = len(compacted_streams["positions"]) // 12
    geometry["index_count"] = len(output_indices) // INDEX.size
    geometry["subsets"] = subsets
    geometry["scene"]["mesh_placements"] = placement_records
    attribute_bytes = sum(len(stream) for stream in compacted_streams.values())
    report = {"source_triangles": source_triangles, "target_triangles": target_triangles,
        "triangles": actual_triangles, "maximum_error": maximum_error, "ratio": float(ratio),
        "source_vertices": vertex_count, "vertices": geometry["vertex_count"],
        "source_attribute_bytes": source_attribute_bytes, "attribute_bytes": attribute_bytes}
    return geometry, report


def test_fixture() -> dict:
    """Make a deterministic curved grid split into two contiguous material regions."""
    import random
    import struct

    side = 25
    positions = bytearray()
    normals = bytearray()
    uvs = bytearray()
    tangents = bytearray()
    for y in range(side):
        for x in range(side):
            z = math.sin(x * 0.3) * math.cos(y * 0.2) * 0.1
            position = (float(x), float(y), z)
            positions += struct.pack("<3f", *position)
            normals += struct.pack("<3f", *position)
            uvs += struct.pack("<2f", float(x), float(y))
            tangents += struct.pack("<4f", *position, 1.0)
    left, right = [], []
    for y in range(side - 1):
        for x in range(side - 1):
            a = y * side + x
            b, c, d = a + 1, a + side, a + side + 1
            target = left if x < (side - 1) // 2 else right
            target.extend(((a, c, b), (b, c, d)))
    random.Random(233).shuffle(left)
    random.Random(811).shuffle(right)
    left = [index for triangle in left for index in triangle]
    right = [index for triangle in right for index in triangle]
    indices = left + right
    first_count = len(left)
    return {"positions": bytes(positions), "normals": bytes(normals), "uvs": bytes(uvs),
        "tangents": bytes(tangents), "indices": struct.pack(f"<{len(indices)}I", *indices),
        "vertex_count": side * side, "subsets": [(0, first_count, 0), (first_count, len(right), 1)],
        "scene": {"mesh_placements": [{"mesh": 0, "node": 0, "vertex_start": 0,
            "vertex_count": side * side, "index_start": 0, "index_count": len(indices),
            "subset_start": 0, "subset_count": 2, "transform": ()}]},
        "skin": None, "animation_clips": [], "morph_targets": []}


def write_test_source(directory: Path) -> Path:
    """Write the curved two-material grid as a self-contained glTF fixture."""
    geometry = test_fixture()
    positions = geometry["positions"]
    point_rows = list(struct.iter_unpack("<3f", positions))
    data = bytearray(positions)
    buffer_views = [{"buffer": 0, "byteOffset": 0, "byteLength": len(positions)}]
    accessors = [{"bufferView": 0, "componentType": 5126, "count": geometry["vertex_count"],
        "type": "VEC3", "min": [min(row[axis] for row in point_rows) for axis in range(3)],
        "max": [max(row[axis] for row in point_rows) for axis in range(3)]}]
    primitives = []
    for material, (start, count, _) in enumerate(geometry["subsets"]):
        indices = geometry["indices"][start * INDEX.size:(start + count) * INDEX.size]
        offset = len(data)
        data += indices
        buffer_view = len(buffer_views)
        buffer_views.append({"buffer": 0, "byteOffset": offset, "byteLength": len(indices)})
        accessor = len(accessors)
        accessors.append({"bufferView": buffer_view, "componentType": 5125,
            "count": count, "type": "SCALAR"})
        primitives.append({"attributes": {"POSITION": 0}, "indices": accessor,
            "material": material, "mode": 4})
    document = {"asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": [0, 1]}],
        "nodes": [{"mesh": 0}, {"mesh": 0}], "meshes": [{"primitives": primitives}],
        "materials": [{"name": "left"}, {"name": "right"}],
        "buffers": [{"byteLength": len(data), "uri": "data:application/octet-stream;base64," +
            base64.b64encode(data).decode("ascii")}], "bufferViews": buffer_views, "accessors": accessors}
    path = directory / "lod-grid.gltf"
    path.write_text(json.dumps(document, separators=(",", ":")), encoding="utf-8")
    return path


def write_test_source_with_unused_vertices(directory: Path) -> Path:
    """Keep the position accessor intact while retaining only its first primitive."""
    path = write_test_source(directory)
    document = json.loads(path.read_text(encoding="utf-8"))
    document["meshes"][0]["primitives"] = document["meshes"][0]["primitives"][:1]
    document["materials"] = document["materials"][:1]
    buffer_data = bytearray(base64.b64decode(
        document["buffers"][0]["uri"].split(",", 1)[1], validate=True))
    position_count = document["accessors"][0]["count"]
    normal_data = struct.pack("<3f", 0.0, 0.0, 1.0) * position_count
    normal_offset = (len(buffer_data) + 3) & ~3
    buffer_data.extend(b"\0" * (normal_offset - len(buffer_data)))
    buffer_data.extend(normal_data)
    normal_view = len(document["bufferViews"])
    document["bufferViews"].append({"buffer": 0, "byteOffset": normal_offset,
        "byteLength": len(normal_data)})
    normal_accessor = len(document["accessors"])
    document["accessors"].append({"bufferView": normal_view, "componentType": 5126,
        "count": position_count, "type": "VEC3"})
    document["meshes"][0]["primitives"][0]["attributes"]["NORMAL"] = normal_accessor
    document["buffers"][0]["byteLength"] = len(buffer_data)
    document["buffers"][0]["uri"] = "data:application/octet-stream;base64," + \
        base64.b64encode(buffer_data).decode("ascii")
    path.write_text(json.dumps(document, separators=(",", ":")), encoding="utf-8")
    return path


def self_test() -> int:
    """Exercise deterministic simplification over two materials and one placement."""
    import sys

    source = test_fixture()
    first, report = simplify_geometry({**source, "scene": {"mesh_placements": [dict(source["scene"]["mesh_placements"][0])]}}, 0.5)
    second, repeated = simplify_geometry({**source, "scene": {"mesh_placements": [dict(source["scene"]["mesh_placements"][0])]}}, 0.5)
    if (first["indices"] != second["indices"] or report != repeated or
            report["triangles"] >= report["source_triangles"] or
            len(first["subsets"]) != 2 or [subset[2] for subset in first["subsets"]] != [0, 1] or
            first["scene"]["mesh_placements"][0]["index_count"] != first["index_count"] or
            first["vertex_count"] >= source["vertex_count"] or
            first["scene"]["mesh_placements"][0]["vertex_count"] != first["vertex_count"]):
        print("glTF LOD self-test failed: simplification was not deterministic or crossed material ranges", file=sys.stderr)
        return 1
    compacted_positions = list(struct.iter_unpack("<3f", first["positions"]))
    compacted_normals = list(struct.iter_unpack("<3f", first["normals"]))
    compacted_uvs = list(struct.iter_unpack("<2f", first["uvs"]))
    compacted_tangents = list(struct.iter_unpack("<4f", first["tangents"]))
    if (len(compacted_positions) != first["vertex_count"] or
            compacted_normals != compacted_positions or
            compacted_uvs != [row[:2] for row in compacted_positions] or
            compacted_tangents != [(*row, 1.0) for row in compacted_positions] or
            any(index >= first["vertex_count"] for (index,) in struct.iter_unpack("<I", first["indices"]))):
        print("glTF LOD self-test failed: compacted vertex attributes or remapped indices diverged", file=sys.stderr)
        return 1
    vertex_count = source["vertex_count"]
    original_indices = list(struct.iter_unpack("<I", source["indices"]))
    duplicated_indices = source["indices"] + struct.pack(f"<{len(original_indices)}I",
        *(index + vertex_count for (index,) in original_indices))
    first_index_count = len(source["indices"]) // INDEX.size
    duplicated = {**source, "positions": source["positions"] * 2,
        "normals": source["normals"] * 2, "uvs": source["uvs"] * 2,
        "tangents": source["tangents"] * 2,
        "vertex_count": vertex_count * 2, "indices": duplicated_indices,
        "subsets": source["subsets"] + [(start + first_index_count, count, slot)
            for start, count, slot in source["subsets"]],
        "scene": {"mesh_placements": [
            dict(source["scene"]["mesh_placements"][0]),
            {"mesh": 0, "node": 1, "vertex_start": vertex_count, "vertex_count": vertex_count,
                "index_start": first_index_count, "index_count": first_index_count,
                "subset_start": 2, "subset_count": 2, "transform": ()}]}}
    placed, placed_report = simplify_geometry(duplicated, 0.5)
    first_placement, second_placement = placed["scene"]["mesh_placements"]
    first_indices = list(struct.iter_unpack("<I", placed["indices"][
        first_placement["index_start"] * INDEX.size:
        (first_placement["index_start"] + first_placement["index_count"]) * INDEX.size]))
    second_indices = list(struct.iter_unpack("<I", placed["indices"][
        second_placement["index_start"] * INDEX.size:
        (second_placement["index_start"] + second_placement["index_count"]) * INDEX.size]))
    if (len(placed["subsets"]) != 4 or placed_report["triangles"] * 2 != placed_report["source_triangles"] or
            any(row["index_count"] != placed["scene"]["mesh_placements"][0]["index_count"]
                for row in placed["scene"]["mesh_placements"]) or
            [row[2] for row in placed["subsets"]] != [0, 1, 0, 1] or
            first_placement["vertex_start"] != 0 or
            second_placement["vertex_start"] != first_placement["vertex_count"] or
            any(index[0] < first_placement["vertex_start"] or index[0] >=
                first_placement["vertex_start"] + first_placement["vertex_count"] for index in first_indices) or
            any(index[0] < second_placement["vertex_start"] or index[0] >=
                second_placement["vertex_start"] + second_placement["vertex_count"] for index in second_indices)):
        print("glTF LOD self-test failed: a simplification range crossed a placement boundary", file=sys.stderr)
        return 1
    try:
        simplify_geometry({**source, "skin": {"joints": []}}, 0.5)
    except ValueError:
        pass
    else:
        print("glTF LOD self-test failed: animated geometry was accepted", file=sys.stderr)
        return 1
    tiny = {**source, "indices": source["indices"][:3 * INDEX.size],
        "subsets": [(0, 3, 0)], "scene": {"mesh_placements": [
            {"index_start": 0, "index_count": 3}]}}
    try:
        simplify_geometry(tiny, 0.5)
    except ValueError as failure:
        if "no placement/material range" not in str(failure):
            print("glTF LOD self-test failed: no-op simplification reported the wrong failure", file=sys.stderr)
            return 1
    else:
        print("glTF LOD self-test failed: a no-op reduction was accepted as an LOD", file=sys.stderr)
        return 1
    chunks = _placement_chunks(source)
    packet = bytearray(HEADER.pack(INPUT_MAGIC, source["vertex_count"],
        len(source["indices"]) // INDEX.size, len(chunks), 0.5, MAX_LOD_ERROR))
    for _, start, count, _ in chunks:
        packet += RANGE.pack(start, count, max(1, math.floor(count / 3 * 0.5)) * 3)
    packet += source["positions"] + source["indices"]
    index_offset = HEADER.size + len(chunks) * RANGE.size + len(source["positions"])
    bad_index = bytearray(packet)
    struct.pack_into("<I", bad_index, index_offset, source["vertex_count"])
    bad_target = bytearray(packet)
    struct.pack_into("<I", bad_target, HEADER.size + 8, 0)
    for malformed in (bad_index, bad_target):
        rejected = subprocess.run([str(_build_cooker())], input=malformed,
            capture_output=True, check=False)
        if rejected.returncode == 0:
            print("glTF LOD self-test failed: native cooker accepted malformed indices or targets", file=sys.stderr)
            return 1
    print(f"glTF LOD self-test passed: {report['source_triangles']} -> {report['triangles']} triangles, "
        f"{report['source_vertices']} -> {report['vertices']} vertices, "
        f"attributes {report['source_attribute_bytes']} -> {report['attribute_bytes']} bytes; "
        f"two material subsets retained; max error {report['maximum_error']:.5f}; malformed packets rejected")
    return 0
