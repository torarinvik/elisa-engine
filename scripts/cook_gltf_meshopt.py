"""Run the pinned meshoptimizer vertex-cache stage for cooked glTF geometry."""

from __future__ import annotations

from collections import Counter
import hashlib
import math
import os
from pathlib import Path
import random
import shutil
import struct
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
MESHOPT_ROOT = ROOT / "dependencies/meshoptimizer"
MAX_VERTICES = 2_000_000
MAX_INDICES = 15_000_000
MAX_SUBSETS = 16
MAX_PLACEMENTS = 256
MAX_RANGES = MAX_SUBSETS * MAX_PLACEMENTS
PACKET_MAGIC = b"ELISAMO1"
PACKET_HEADER = struct.Struct("<8sIII")
RANGE_RECORD = struct.Struct("<III")
REPORT_RECORD = struct.Struct("<ffII")
INDEX_RECORD = struct.Struct("<I")
COOKER_SOURCES = (
    "native/meshopt_index_cooker.cpp",
    "dependencies/meshoptimizer/meshoptimizer.h",
    "dependencies/meshoptimizer/vcacheoptimizer.cpp",
    "dependencies/meshoptimizer/indexanalyzer.cpp",
    "dependencies/meshoptimizer/indexgenerator.cpp",
    "dependencies/meshoptimizer/allocator.cpp",
)


def _build_cooker() -> Path:
    source_paths = [ROOT / source for source in COOKER_SOURCES]
    if not all(path.is_file() for path in source_paths):
        raise ValueError("missing pinned meshoptimizer files; run scripts/fetch_dependencies.py")
    compiler = os.environ.get("CXX", "c++")
    compiler_path = shutil.which(compiler)
    if compiler_path is None:
        raise ValueError(f"C++ compiler is unavailable: {compiler}")
    version = subprocess.run([compiler_path, "--version"], capture_output=True,
        text=True, check=False)
    if version.returncode != 0:
        raise ValueError(version.stderr or version.stdout or "cannot identify C++ compiler")
    fingerprint = hashlib.sha256()
    fingerprint.update(compiler_path.encode("utf-8"))
    fingerprint.update(version.stdout.encode("utf-8"))
    fingerprint.update(os.environ.get("DEVELOPER_DIR", "").encode("utf-8"))
    fingerprint.update(Path(__file__).read_bytes())
    for path in source_paths:
        fingerprint.update(path.read_bytes())
    digest = fingerprint.hexdigest()
    output = ROOT / "build/meshopt-index-cooker"
    stamp = ROOT / "build/meshopt-index-cooker.sha256"
    if output.is_file() and stamp.is_file() and stamp.read_text(encoding="ascii").strip() == digest:
        return output
    output.parent.mkdir(parents=True, exist_ok=True)
    command = [compiler_path, "-std=c++17", "-O2", "-DNDEBUG", "-I", str(MESHOPT_ROOT),
        str(source_paths[0]), *(str(path) for path in source_paths[2:]), "-o", str(output)]
    built = subprocess.run(command, capture_output=True, text=True, check=False)
    if built.returncode != 0 or not output.is_file():
        raise ValueError(built.stderr or built.stdout or "meshoptimizer cooker build failed")
    stamp.write_text(digest + "\n", encoding="ascii")
    return output


def _run_index_cooker(vertex_count: int, indices: bytes,
        chunks: list[tuple[int, int, int]]) -> tuple[bytes, tuple[float, float, int, int]]:
    index_count = len(indices) // INDEX_RECORD.size
    if (vertex_count <= 0 or vertex_count > MAX_VERTICES or not indices or
            len(indices) % INDEX_RECORD.size != 0 or index_count > MAX_INDICES or
            index_count % 3 != 0 or not chunks or len(chunks) > MAX_RANGES):
        raise ValueError("glTF meshoptimizer input exceeds bounded geometry limits")
    covered = 0
    records = bytearray()
    for start, count, material_slot in chunks:
        if (start != covered or count <= 0 or count % 3 != 0 or
                count > index_count - covered or not 0 <= material_slot < MAX_SUBSETS):
            raise ValueError("glTF meshoptimizer chunks must partition material triangles")
        records += RANGE_RECORD.pack(start, count, material_slot)
        covered += count
    if covered != index_count:
        raise ValueError("glTF meshoptimizer chunks do not cover the index stream")
    packet = PACKET_HEADER.pack(PACKET_MAGIC, vertex_count, index_count, len(chunks))
    packet += records + indices
    cooker = _build_cooker()
    optimized = subprocess.run([str(cooker)], input=packet, capture_output=True, check=False)
    if optimized.returncode != 0:
        detail = optimized.stderr.decode("utf-8", errors="replace").strip()
        raise ValueError(detail or "meshoptimizer rejected cooked glTF geometry")
    response = optimized.stdout
    expected_size = len(packet) + REPORT_RECORD.size
    fixed_prefix = PACKET_HEADER.size + len(records)
    if len(response) != expected_size or response[:fixed_prefix] != packet[:fixed_prefix]:
        raise ValueError("meshoptimizer returned an invalid glTF cooker response")
    report = REPORT_RECORD.unpack_from(response, len(packet))
    if (not all(math.isfinite(value) for value in report[:2]) or report[0] < 0 or report[1] < 0 or
            report[1] > report[0] or report[2] > report[3] or report[3] != len(chunks)):
        raise ValueError("meshoptimizer returned invalid cache statistics")
    return response[fixed_prefix:len(packet)], report


def _index_chunks(indices: bytes, vertex_count: int,
        chunks: list[tuple[int, int, int]]) -> bytes:
    return _run_index_cooker(vertex_count, indices, chunks)[0]


def optimize_geometry(geometry: dict) -> dict:
    indices = geometry["indices"]
    subsets = geometry["subsets"]
    placements = geometry["scene"]["mesh_placements"]
    chunks = []
    for placement in placements:
        placement_start = placement["index_start"]
        placement_end = placement_start + placement["index_count"]
        for subset_start, subset_count, material_slot in subsets:
            start = max(placement_start, subset_start)
            end = min(placement_end, subset_start + subset_count)
            if start < end:
                chunks.append((start, end - start, material_slot))
    geometry["indices"] = _index_chunks(indices, geometry["vertex_count"], chunks)
    return geometry


def optimize_index_chunks(vertex_count: int, indices: bytes,
        chunks: list[tuple[int, int, int]]) -> tuple[bytes, tuple[float, float, int, int]]:
    return _run_index_cooker(vertex_count, indices, chunks)


def _grid_indices(cells_per_side: int, vertex_offset: int, seed: int) -> list[int]:
    side = cells_per_side + 1
    triangles = []
    for y in range(cells_per_side):
        for x in range(cells_per_side):
            a = vertex_offset + y * side + x
            b = a + 1
            c = a + side
            d = c + 1
            triangles.extend(((a, c, b), (b, c, d)))
    random.Random(seed).shuffle(triangles)
    return [index for triangle in triangles for index in triangle]


def _triangle_multiset(indices: bytes) -> Counter:
    values = struct.unpack(f"<{len(indices) // INDEX_RECORD.size}I", indices)
    return Counter(tuple(values[offset:offset + 3]) for offset in range(0, len(values), 3))


def self_test() -> int:
    cells_per_side = 16
    vertices_per_grid = (cells_per_side + 1) ** 2
    first = _grid_indices(cells_per_side, 0, 79)
    second = _grid_indices(cells_per_side, vertices_per_grid, 131)
    source = struct.pack(f"<{len(first + second)}I", *(first + second))
    first_count = len(first)
    chunks = [(0, first_count, 0), (first_count, len(second), 1)]
    optimized, report = optimize_index_chunks(vertices_per_grid * 2, source, chunks)
    repeated, repeated_report = optimize_index_chunks(vertices_per_grid * 2, source, chunks)
    if (optimized != repeated or report != repeated_report or report[1] >= report[0] or
            report[2] != 2 or report[3] != 2):
        print("glTF meshoptimizer self-test failed: cache reorder did not improve deterministically",
            file=sys.stderr)
        return 1
    if (_triangle_multiset(optimized[:first_count * INDEX_RECORD.size]) !=
            _triangle_multiset(source[:first_count * INDEX_RECORD.size]) or
            _triangle_multiset(optimized[first_count * INDEX_RECORD.size:]) !=
            _triangle_multiset(source[first_count * INDEX_RECORD.size:])):
        print("glTF meshoptimizer self-test failed: a placement/material chunk changed triangles",
            file=sys.stderr)
        return 1
    placements = [{"index_start": 0, "index_count": first_count},
        {"index_start": first_count, "index_count": len(second)}]
    integrated = optimize_geometry({"indices": source, "vertex_count": vertices_per_grid * 2,
        "subsets": [(0, first_count + len(second), 0)], "scene": {"mesh_placements": placements}})
    split_optimized, split_report = optimize_index_chunks(vertices_per_grid * 2, source,
        [(0, first_count, 0), (first_count, len(second), 0)])
    if (integrated["indices"] != split_optimized or integrated["scene"]["mesh_placements"] != placements or
            split_report[1] >= split_report[0] or split_report[2] != len(placements)):
        print("glTF meshoptimizer self-test failed: a merged material subset crossed placements",
            file=sys.stderr)
        return 1
    try:
        optimize_index_chunks(vertices_per_grid * 2, source,
            [(0, first_count, 0), (first_count - 3, len(second) + 3, 1)])
    except ValueError:
        pass
    else:
        print("glTF meshoptimizer self-test failed: overlapping placement ranges were accepted",
            file=sys.stderr)
        return 1
    malformed = bytearray(source)
    struct.pack_into("<I", malformed, len(malformed) - INDEX_RECORD.size, vertices_per_grid * 2)
    try:
        optimize_index_chunks(vertices_per_grid * 2, bytes(malformed), chunks)
    except ValueError:
        print(f"glTF meshoptimizer self-test passed: ACMR {report[0]:.4f} -> {report[1]:.4f}; "
            "placement/material ranges and malformed indices checked")
        return 0
    print("glTF meshoptimizer self-test failed: an out-of-range vertex index was accepted",
        file=sys.stderr)
    return 1
