#!/usr/bin/env python3
"""Cook static glTF geometry into a bounded, collision-only package."""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import math
from pathlib import Path
import struct
import sys
import tempfile

import cook_assets
import cook_gltf_geometry
from elisa_package import write_package


MAX_VERTICES = 65_536
MAX_INDICES = 196_608
MAX_GEOMETRY_BYTES = 8 * 1024 * 1024
MAX_COORDINATE = 10_000.0
POSITION_STRIDE = 12
INDEX_STRIDE = 4


def _triangle_has_area(a: tuple[float, float, float], b: tuple[float, float, float],
        c: tuple[float, float, float]) -> bool:
    ab = tuple(b[axis] - a[axis] for axis in range(3))
    ac = tuple(c[axis] - a[axis] for axis in range(3))
    cross = (ab[1] * ac[2] - ab[2] * ac[1],
        ab[2] * ac[0] - ab[0] * ac[2],
        ab[0] * ac[1] - ab[1] * ac[0])
    return sum(value * value for value in cross) > 1.0e-20


def _has_volume(vertices: list[tuple[float, float, float]]) -> bool:
    if len(vertices) < 4:
        return False
    origin = vertices[0]
    extent = max(max(vertex[axis] for vertex in vertices) -
        min(vertex[axis] for vertex in vertices) for axis in range(3))
    if not math.isfinite(extent) or extent <= 1.0e-6:
        return False
    volume_limit = extent * extent * extent * 1.0e-9
    for first in range(1, len(vertices) - 2):
        ab = tuple(vertices[first][axis] - origin[axis] for axis in range(3))
        for second in range(first + 1, len(vertices) - 1):
            ac = tuple(vertices[second][axis] - origin[axis] for axis in range(3))
            normal = (ab[1] * ac[2] - ab[2] * ac[1],
                ab[2] * ac[0] - ab[0] * ac[2],
                ab[0] * ac[1] - ab[1] * ac[0])
            for third in range(second + 1, len(vertices)):
                ad = tuple(vertices[third][axis] - origin[axis] for axis in range(3))
                if abs(sum(normal[axis] * ad[axis] for axis in range(3))) > volume_limit:
                    return True
    return False


def _collision_streams(geometry: dict, shape: str) -> tuple[bytes, bytes, int]:
    positions_raw = geometry["positions"]
    indices_raw = geometry["indices"]
    if len(positions_raw) % POSITION_STRIDE or len(indices_raw) % INDEX_STRIDE:
        raise ValueError("normalized collision streams have invalid strides")
    source_vertices = [tuple(vertex) for vertex in struct.iter_unpack("<3f", positions_raw)]
    source_indices = [row[0] for row in struct.iter_unpack("<I", indices_raw)]
    if len(source_indices) % 3:
        raise ValueError("collision geometry must contain indexed triangles")

    unique_vertices: list[tuple[float, float, float]] = []
    vertex_map: dict[tuple[float, float, float], int] = {}
    remap: list[int] = []
    for vertex in source_vertices:
        if not all(math.isfinite(value) and abs(value) <= MAX_COORDINATE for value in vertex):
            raise ValueError("collision vertex is non-finite or exceeds the runtime coordinate bound")
        target = vertex_map.get(vertex)
        if target is None:
            target = len(unique_vertices)
            vertex_map[vertex] = target
            unique_vertices.append(vertex)
        remap.append(target)

    retained_indices: list[int] = []
    removed_degenerate = 0
    for offset in range(0, len(source_indices), 3):
        source_triangle = source_indices[offset:offset + 3]
        if any(index >= len(remap) for index in source_triangle):
            raise ValueError("normalized collision index is outside the position stream")
        triangle = tuple(remap[index] for index in source_triangle)
        if len(set(triangle)) != 3 or not _triangle_has_area(*(unique_vertices[index] for index in triangle)):
            removed_degenerate += 1
            continue
        retained_indices.extend(triangle)

    if len(unique_vertices) > MAX_VERTICES or len(retained_indices) > MAX_INDICES:
        raise ValueError("collision geometry exceeds the runtime vertex or index bound")
    minimum_vertices = 4 if shape == "convex_hull" else 3
    if len(unique_vertices) < minimum_vertices:
        raise ValueError("collision geometry has too few unique vertices")
    if not retained_indices:
        raise ValueError("collision geometry has no non-degenerate triangles")
    if shape == "convex_hull" and not _has_volume(unique_vertices):
        raise ValueError("convex hull collision geometry must enclose non-zero volume")

    positions = b"".join(struct.pack("<3f", *vertex) for vertex in unique_vertices)
    indices = struct.pack(f"<{len(retained_indices)}I", *retained_indices)
    if len(positions) + len(indices) > MAX_GEOMETRY_BYTES:
        raise ValueError("collision geometry exceeds the 8 MiB runtime memory bound")
    return positions, indices, removed_degenerate


def cook_collision_package(source_path: Path, asset_path: str, output_path: Path,
        shape: str, simplify_ratio: float | None = None,
        dependencies: tuple[str, ...] = ()) -> tuple[Path, dict]:
    if shape not in ("convex_hull", "triangle_mesh"):
        raise ValueError("collision shape must be convex_hull or triangle_mesh")
    asset_path = cook_gltf_geometry.safe_asset_path(asset_path)
    if simplify_ratio is not None and not 0.05 <= simplify_ratio < 1.0:
        raise ValueError("collision simplify ratio must be at least 0.05 and below 1.0")
    source_path = source_path.expanduser().resolve(strict=True)
    if (not source_path.is_file() or source_path.stat().st_size == 0 or
            source_path.stat().st_size > cook_assets.MAX_DOCUMENT_BYTES):
        raise ValueError("glTF source is not a regular file within the 64 MiB source bound")
    source_bytes = source_path.read_bytes()
    document = cook_assets.read_gltf(source_bytes)
    geometry = cook_gltf_geometry.normalized_geometry(document,
        cook_assets.source_bytes(source_path.parent, document), simplify_ratio)
    if (geometry["skin"] is not None or geometry["animation_clips"] or
            geometry["morph_targets"] or geometry["morph_default_weights"]):
        raise ValueError("collision cooking accepts only static geometry")
    positions, indices, removed_degenerate = _collision_streams(geometry, shape)
    vertex_count = len(positions) // POSITION_STRIDE
    index_count = len(indices) // INDEX_STRIDE
    output_path = output_path.expanduser().resolve()
    if output_path == source_path:
        raise ValueError("collision output must be distinct from its glTF source")
    output_path.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "format=elisa-physics-collision-v1",
        f"source={asset_path}",
        "source_sha256=" + hashlib.sha256(source_bytes).hexdigest(),
        f"shape={shape}",
        f"vertices={vertex_count}",
        f"indices={index_count}",
        f"position_stride={POSITION_STRIDE}",
        f"index_stride={INDEX_STRIDE}",
        "positions_b64=" + base64.b64encode(positions).decode("ascii"),
        "indices_b64=" + base64.b64encode(indices).decode("ascii"),
    ]
    package = ("\n".join(lines) + "\n").encode("ascii")
    if len(package) > 64 * 1024 * 1024:
        raise ValueError("collision package exceeds the 64 MiB runtime file bound")
    if output_path.suffix.lower() == ".elpk":
        write_package(output_path, {"collision": package}, dependencies)
    else:
        if dependencies:
            raise ValueError("collision package dependencies require an .elpk output")
        output_path.write_bytes(package)
    return output_path, {
        "shape": shape,
        "vertices": vertex_count,
        "indices": index_count,
        "triangles": index_count // 3,
        "removed_degenerate_triangles": removed_degenerate,
        "geometry_bytes": len(positions) + len(indices),
        "source_sha256": hashlib.sha256(source_bytes).hexdigest(),
    }


def write_tetrahedron_fixture(path: Path) -> None:
    positions = (0.0, 0.0, 0.0, 1.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0, 0.0, 1.0)
    indices = (0, 2, 1, 0, 1, 3, 0, 3, 2, 1, 2, 3)
    mesh = struct.pack("<12f12I", *positions, *indices)
    encoded = base64.b64encode(mesh).decode("ascii")
    document = {
        "asset": {"version": "2.0"},
        "buffers": [{"uri": f"data:application/octet-stream;base64,{encoded}",
            "byteLength": len(mesh)}],
        "bufferViews": [
            {"buffer": 0, "byteOffset": 0, "byteLength": 48, "target": 34962},
            {"buffer": 0, "byteOffset": 48, "byteLength": 48, "target": 34963},
        ],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": 4, "type": "VEC3",
                "min": [0.0, 0.0, 0.0], "max": [1.0, 1.0, 1.0]},
            {"bufferView": 1, "componentType": 5125, "count": 12, "type": "SCALAR"},
        ],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0}, "indices": 1}]}],
        "nodes": [{"mesh": 0}],
        "scenes": [{"nodes": [0]}],
        "scene": 0,
    }
    path.write_text(json.dumps(document, separators=(",", ":")), encoding="utf-8")


def self_test() -> int:
    with tempfile.TemporaryDirectory(prefix="elisa-physics-collision-") as temporary:
        folder = Path(temporary)
        source = folder / "tetra.gltf"
        write_tetrahedron_fixture(source)
        first, first_result = cook_collision_package(source, "test/tetra.gltf",
            folder / "first.collision", "convex_hull")
        second, second_result = cook_collision_package(source, "test/tetra.gltf",
            folder / "second.collision", "convex_hull")
        triangle, triangle_result = cook_collision_package(source, "test/tetra.gltf",
            folder / "triangle.collision", "triangle_mesh")
        bundled, _ = cook_collision_package(source, "test/tetra.gltf",
            folder / "tetra.elpk", "triangle_mesh", dependencies=("base.elpk",))
        contents = first.read_bytes()
        if (first_result != second_result or contents != second.read_bytes() or
                first_result["vertices"] != 4 or first_result["triangles"] != 4 or
                first_result["removed_degenerate_triangles"] != 0 or
                triangle_result["triangles"] != 4 or b"shape=convex_hull\n" not in contents or
                triangle.read_bytes() == contents or
                bundled.read_bytes()[:4] != b"ELPK"):
            print("physics collision cooker self-test failed: deterministic tetrahedron mismatch",
                file=sys.stderr)
            return 1
        try:
            cook_collision_package(source, "../unsafe.gltf", folder / "bad.collision",
                "triangle_mesh")
        except ValueError:
            pass
        else:
            print("physics collision cooker self-test accepted an unsafe asset path", file=sys.stderr)
            return 1
    print("Physics collision cooker self-test passed: deterministic convex and triangle packages.")
    return 0


def main(arguments: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", nargs="?", type=Path)
    parser.add_argument("--asset-path", help="safe project-relative source identity")
    parser.add_argument("--output", type=Path, help="destination collision package")
    parser.add_argument("--shape", choices=("convex_hull", "triangle_mesh"),
        help="collision shape the runtime will cook")
    parser.add_argument("--simplify-ratio", type=float,
        help="reduce static collision triangles to approximately this fraction")
    parser.add_argument("--dependency", action="append", default=[], metavar="BUNDLE",
        help="name a bundle this .elpk output needs, relative to the output's directory")
    parser.add_argument("--self-test", action="store_true")
    options = parser.parse_args(arguments)
    if options.self_test:
        if any((options.source, options.asset_path, options.output, options.shape,
                options.simplify_ratio is not None, options.dependency)):
            parser.error("--self-test cannot be combined with cook options")
        return self_test()
    if options.source is None or options.asset_path is None or options.output is None or options.shape is None:
        parser.error("source, --asset-path, --output, and --shape are required")
    try:
        output, result = cook_collision_package(options.source, options.asset_path,
            options.output, options.shape, options.simplify_ratio, tuple(options.dependency))
    except (OSError, RuntimeError, ValueError, KeyError, IndexError, TypeError, AttributeError) as failure:
        print(f"physics collision cooking failed: {failure}", file=sys.stderr)
        return 1
    print(f"cooked {result['shape']} collision {options.asset_path} -> {output} "
        f"({result['vertices']} vertices, {result['triangles']} triangles, "
        f"{result['geometry_bytes']} geometry bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
