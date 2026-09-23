"""Run the pinned MikkTSpace stage for glTF cooker geometry."""

from __future__ import annotations

import hashlib
import math
import os
from array import array
from collections.abc import Iterable
from pathlib import Path
import shutil
import struct
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
MIKK_ROOT = ROOT / "dependencies/mikktspace"
MAX_VERTICES = 2_000_000
MAX_INDICES = 15_000_000
INPUT_MAGIC = b"ELISAMK1"
OUTPUT_MAGIC = b"ELISAMR1"
HEADER = struct.Struct("<8sII")
SOURCES = (
    "native/gltf_mikktspace_cooker.cpp",
    "native/mikktspace_geometry.h",
    "dependencies/mikktspace/mikktspace.c",
    "dependencies/mikktspace/mikktspace.h",
)


def pack_u32(values: Iterable[int]) -> bytes:
    packed = array("I", values)
    if packed.itemsize != 4:
        raise ValueError("the glTF MikkTSpace protocol requires 32-bit unsigned integers")
    if sys.byteorder != "little":
        packed.byteswap()
    return packed.tobytes()


def _unpack_u32(data: bytes) -> array:
    if len(data) % 4 != 0:
        raise ValueError("MikkTSpace returned a misaligned integer stream")
    values = array("I")
    values.frombytes(data)
    if values.itemsize != 4:
        raise ValueError("the glTF MikkTSpace protocol requires 32-bit unsigned integers")
    if sys.byteorder != "little":
        values.byteswap()
    return values


def _build_cooker() -> Path:
    paths = [ROOT / source for source in SOURCES]
    if not all(path.is_file() for path in paths):
        raise ValueError("missing pinned MikkTSpace files; run scripts/fetch_dependencies.py")
    cc = shutil.which(os.environ.get("CC", "cc"))
    cxx = shutil.which(os.environ.get("CXX", "c++"))
    if cc is None or cxx is None:
        raise ValueError("C and C++ compilers are required for the offline MikkTSpace stage")
    versions = []
    for compiler in (cc, cxx):
        version = subprocess.run([compiler, "--version"], capture_output=True, text=True, check=False)
        if version.returncode != 0:
            raise ValueError(version.stderr or version.stdout or "cannot identify the cooker compiler")
        versions.append(version.stdout)
    fingerprint = hashlib.sha256()
    for value in (cc, cxx, *versions, os.environ.get("DEVELOPER_DIR", "")):
        fingerprint.update(value.encode("utf-8"))
        fingerprint.update(b"\0")
    fingerprint.update(Path(__file__).read_bytes())
    for path in paths:
        fingerprint.update(path.read_bytes())
    digest = fingerprint.hexdigest()
    build_dir = ROOT / "build"
    output = build_dir / "gltf-mikktspace-cooker"
    object_file = build_dir / "gltf-mikktspace.o"
    stamp = build_dir / "gltf-mikktspace-cooker.sha256"
    if output.is_file() and stamp.is_file() and stamp.read_text(encoding="ascii").strip() == digest:
        return output
    build_dir.mkdir(parents=True, exist_ok=True)
    compiled = subprocess.run([cc, "-std=c99", "-O2", "-I", str(MIKK_ROOT), "-c",
        str(paths[2]), "-o", str(object_file)], capture_output=True, text=True, check=False)
    if compiled.returncode != 0:
        raise ValueError(compiled.stderr or compiled.stdout or "MikkTSpace C compilation failed")
    linked = subprocess.run([cxx, "-std=c++17", "-O2", "-DNDEBUG", "-I", str(MIKK_ROOT),
        "-I", str(ROOT / "native"), str(paths[0]), str(object_file), "-o", str(output)],
        capture_output=True, text=True, check=False)
    if linked.returncode != 0 or not output.is_file():
        raise ValueError(linked.stderr or linked.stdout or "MikkTSpace cooker build failed")
    stamp.write_text(digest + "\n", encoding="ascii")
    return output


def generate(positions: bytes, normals: bytes, uvs: bytes,
        vertex_count: int, indices: list[int]) -> dict:
    """Generate MikkTSpace frames, splitting vertices at tangent seams."""
    if (type(vertex_count) is not int or vertex_count <= 0 or vertex_count > MAX_VERTICES or
            len(positions) != vertex_count * 12 or len(normals) != vertex_count * 12 or
            len(uvs) != vertex_count * 8 or not indices or len(indices) > MAX_INDICES or
            len(indices) % 3 != 0 or any(type(index) is not int or not 0 <= index < vertex_count
                for index in indices)):
        raise ValueError("glTF MikkTSpace input exceeds bounded geometry limits")
    if any(not math.isfinite(value) for stream in (positions, normals, uvs)
            for (value,) in struct.iter_unpack("<f", stream)):
        raise ValueError("glTF MikkTSpace streams contain non-finite values")
    packet = HEADER.pack(INPUT_MAGIC, vertex_count, len(indices)) + positions + normals + uvs + \
        pack_u32(indices)
    cooked = subprocess.run([str(_build_cooker())], input=packet, capture_output=True, check=False)
    if cooked.returncode != 0:
        detail = cooked.stderr.decode("utf-8", errors="replace").strip()
        raise ValueError(detail or "MikkTSpace rejected the glTF geometry")
    response = cooked.stdout
    if len(response) < HEADER.size:
        raise ValueError("MikkTSpace returned a truncated geometry header")
    magic, output_vertices, output_indices = HEADER.unpack_from(response)
    expected = HEADER.size + output_vertices * 52 + output_indices * 4
    if (magic != OUTPUT_MAGIC or not 0 < output_vertices <= MAX_VERTICES or
            output_indices != len(indices) or len(response) != expected):
        raise ValueError("MikkTSpace returned an invalid geometry layout")
    cursor = HEADER.size

    def take(size: int) -> bytes:
        nonlocal cursor
        result = response[cursor:cursor + size]
        cursor += size
        return result

    cooked_positions = take(output_vertices * 12)
    cooked_normals = take(output_vertices * 12)
    cooked_uvs = take(output_vertices * 8)
    tangents = take(output_vertices * 16)
    cooked_indices = _unpack_u32(take(output_indices * 4))
    source_vertices = _unpack_u32(take(output_vertices * 4))
    if (cursor != len(response) or any(index >= output_vertices for index in cooked_indices) or
            any(index >= vertex_count for index in source_vertices)):
        raise ValueError("MikkTSpace returned out-of-range remapped geometry")
    for tangent, normal in zip(struct.iter_unpack("<4f", tangents),
            struct.iter_unpack("<3f", cooked_normals)):
        length = math.sqrt(sum(component * component for component in tangent[:3]))
        dot = sum(tangent[axis] * normal[axis] for axis in range(3))
        if (not all(math.isfinite(value) for value in tangent) or not math.isfinite(length) or
                abs(length - 1.0) > 0.02 or not math.isfinite(dot) or abs(dot) > 0.02 or
                abs(abs(tangent[3]) - 1.0) > 1.0e-4):
            raise ValueError("MikkTSpace returned an invalid tangent frame")
    return {"positions": cooked_positions, "normals": cooked_normals, "uvs": cooked_uvs,
        "tangents": tangents, "indices": cooked_indices, "source_vertices": source_vertices}


def self_test() -> int:
    positions = struct.pack("<12f", 0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 1, 0)
    normals = struct.pack("<12f", *(value for _ in range(4) for value in (0, 0, 1)))
    uvs = struct.pack("<8f", 0, 0, 1, 0, 0, 1, -1, 1)
    indices = [0, 1, 2, 1, 3, 2]
    try:
        first = generate(positions, normals, uvs, 4, indices)
        repeat = generate(positions, normals, uvs, 4, indices)
    except ValueError as error:
        print(f"glTF MikkTSpace self-test failed: {error}", file=sys.stderr)
        return 1
    tangent_values = list(struct.iter_unpack("<4f", first["tangents"]))
    signs = [tangent_values[vertex][3] for vertex in first["indices"]]
    if (first != repeat or len(first["source_vertices"]) <= 4 or
            signs[1] == signs[3] or len(first["indices"]) != 6):
        print("glTF MikkTSpace self-test failed: mirrored seam did not split deterministically",
            file=sys.stderr)
        return 1
    degenerate_uvs = bytes(4 * 8)
    try:
        degenerate = generate(positions, normals, degenerate_uvs, 4, indices)
    except ValueError as error:
        print(f"glTF MikkTSpace self-test failed: degenerate UV fallback: {error}", file=sys.stderr)
        return 1
    if any(not math.isfinite(value) for (value,) in struct.iter_unpack("<f", degenerate["tangents"])):
        print("glTF MikkTSpace self-test failed: degenerate UV fallback is non-finite", file=sys.stderr)
        return 1
    print(f"glTF MikkTSpace self-test passed: mirrored seam split {4} -> "
        f"{len(first['source_vertices'])} vertices; deterministic finite UV-degenerate fallback")
    return 0


if __name__ == "__main__":
    raise SystemExit(self_test())
