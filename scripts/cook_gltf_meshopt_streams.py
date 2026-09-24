"""Encode cooked glTF vertex and index streams with pinned meshoptimizer."""

from __future__ import annotations

import hashlib
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
MESHOPT_ROOT = ROOT / "dependencies/meshoptimizer"
MAGIC = b"ELISASC1"
HEADER = struct.Struct("<8sII")
STREAM = struct.Struct("<5I")
VERTEX_STREAM = 1
INDEX_STREAM = 2
MAX_PACKET_BYTES = 128 * 1024 * 1024
SOURCES = (
    "native/meshopt_stream_cooker.cpp",
    "native/meshopt_stream_codec.cpp",
    "native/meshopt_stream_codec.h",
    "dependencies/meshoptimizer/meshoptimizer.h",
    "dependencies/meshoptimizer/indexcodec.cpp",
    "dependencies/meshoptimizer/vertexcodec.cpp",
)


def _build_cooker() -> Path:
    source_paths = [ROOT / source for source in SOURCES]
    if not all(path.is_file() for path in source_paths):
        raise ValueError("missing pinned meshoptimizer codec sources; run scripts/fetch_dependencies.py")
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
    for path in source_paths:
        fingerprint.update(path.read_bytes())
    digest = fingerprint.hexdigest()
    output = ROOT / "build/meshopt-stream-cooker"
    stamp = ROOT / "build/meshopt-stream-cooker.sha256"
    if output.is_file() and stamp.is_file() and stamp.read_text(encoding="ascii").strip() == digest:
        return output
    output.parent.mkdir(parents=True, exist_ok=True)
    command = [compiler_path, "-std=c++17", "-O2", "-DNDEBUG", "-I", str(MESHOPT_ROOT),
        str(source_paths[0]), str(source_paths[1]), str(source_paths[4]), str(source_paths[5]),
        "-o", str(output)]
    built = subprocess.run(command, capture_output=True, text=True, check=False)
    if built.returncode != 0 or not output.is_file():
        raise ValueError(built.stderr or built.stdout or "meshoptimizer stream cooker build failed")
    stamp.write_text(digest + "\n", encoding="ascii")
    return output


def encode_streams(streams: list[tuple[str, int, int, int, bytes]]) -> dict[str, bytes]:
    """Encode (name, kind, count, stride, bytes) streams in one bounded call."""
    if not streams or len(streams) > 8 or len({item[0] for item in streams}) != len(streams):
        raise ValueError("meshoptimizer codec stream list is empty, duplicated, or too large")
    vertex_counts = {count for _name, kind, count, _stride, _data in streams if kind == VERTEX_STREAM}
    if len(vertex_counts) != 1:
        raise ValueError("meshoptimizer codec needs vertex streams with one shared vertex count")
    vertex_count = vertex_counts.pop()
    packet = bytearray(HEADER.pack(MAGIC, len(streams), 0))
    for name, kind, count, stride, data in streams:
        if (not name or kind not in (VERTEX_STREAM, INDEX_STREAM) or type(count) is not int or
                type(stride) is not int or count <= 0 or stride <= 0):
            raise ValueError("meshoptimizer codec stream dimensions are invalid")
        if kind == VERTEX_STREAM and (stride > 256 or count > 2_000_000):
            raise ValueError("meshoptimizer vertex stream exceeds its bound")
        if kind == INDEX_STREAM and (stride != 4 or count > 15_000_000 or count % 3 != 0):
            raise ValueError("meshoptimizer index stream exceeds its bound")
        if len(data) != count * stride:
            raise ValueError("meshoptimizer codec stream byte count does not match its dimensions")
        packet += STREAM.pack(kind, count, stride, len(data), vertex_count if kind == INDEX_STREAM else count)
        packet += data
        if len(packet) > MAX_PACKET_BYTES:
            raise ValueError("meshoptimizer codec input exceeds the packet bound")
    cooker = _build_cooker()
    encoded = subprocess.run([str(cooker)], input=packet, capture_output=True, check=False)
    if encoded.returncode != 0:
        detail = encoded.stderr.decode("utf-8", errors="replace").strip()
        raise ValueError(detail or "meshoptimizer stream encoding failed")
    response = encoded.stdout
    if (len(response) < HEADER.size or len(response) > MAX_PACKET_BYTES or
            HEADER.unpack_from(response) != (MAGIC, len(streams), 0)):
        raise ValueError("meshoptimizer returned an invalid stream header")
    offset = HEADER.size
    result: dict[str, bytes] = {}
    for name, kind, count, stride, _data in streams:
        if len(response) - offset < STREAM.size:
            raise ValueError("meshoptimizer returned a truncated stream descriptor")
        found_kind, found_count, found_stride, byte_count, vertex_count = STREAM.unpack_from(response, offset)
        offset += STREAM.size
        expected_vertex_count = vertex_count if kind == INDEX_STREAM else count
        if ((found_kind, found_count, found_stride, vertex_count) !=
                (kind, count, stride, expected_vertex_count) or byte_count == 0 or
                byte_count > len(response) - offset):
            raise ValueError("meshoptimizer returned inconsistent stream dimensions")
        result[name] = response[offset:offset + byte_count]
        offset += byte_count
    if offset != len(response):
        raise ValueError("meshoptimizer returned trailing stream bytes")
    return result


def self_test() -> int:
    import math

    vertex_count = 256
    positions = b"".join(struct.pack("<3f", index % 16, index // 16, math.sin(index * 0.01))
        for index in range(vertex_count))
    normals = struct.pack("<3f", 0.0, 0.0, 1.0) * vertex_count
    uvs = b"".join(struct.pack("<2f", (index % 16) / 15, (index // 16) / 15)
        for index in range(vertex_count))
    indices = b"".join(struct.pack("<I", value) for row in range(15) for col in range(15)
        for value in (row * 16 + col, (row + 1) * 16 + col, row * 16 + col + 1,
            row * 16 + col + 1, (row + 1) * 16 + col, (row + 1) * 16 + col + 1))
    streams = [("positions", VERTEX_STREAM, vertex_count, 12, positions),
        ("normals", VERTEX_STREAM, vertex_count, 12, normals),
        ("uvs", VERTEX_STREAM, vertex_count, 8, uvs),
        ("indices", INDEX_STREAM, len(indices) // 4, 4, indices)]
    first = encode_streams(streams)
    second = encode_streams(streams)
    if first != second or any(len(first[name]) >= len(data) for name, _kind, _count, _stride, data in streams):
        print("meshoptimizer stream codec self-test failed: output is not smaller and deterministic",
            file=sys.stderr)
        return 1
    try:
        encode_streams([(name, kind, count, stride, data[:-1])
            for name, kind, count, stride, data in streams])
    except ValueError:
        print("meshoptimizer stream codec self-test passed: deterministic lossless stream encoding")
        return 0
    print("meshoptimizer stream codec self-test failed: truncated input was accepted", file=sys.stderr)
    return 1
