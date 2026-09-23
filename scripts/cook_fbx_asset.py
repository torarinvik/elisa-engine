#!/usr/bin/env python3
"""Cook one bounded FBX mesh into Elisa's existing cooked geometry package."""

from __future__ import annotations

import argparse
import base64
import binascii
import hashlib
import math
import os
from pathlib import Path, PurePosixPath
import re
import struct
import subprocess
import sys
import tempfile

import cook_gltf_animation
from elisa_package import write_geometry_package
from fbx_test_fixtures import write_two_material_mesh, write_two_mesh_scene


ROOT = Path(__file__).resolve().parents[1]
MAX_PACKAGE_BYTES = 64 * 1024 * 1024
MAX_LINE_BYTES = 16 * 1024 * 1024
MAX_FILE_BYTES = 512 * 1024 * 1024


def run(command: list[str]) -> None:
    print("+", " ".join(repr(argument) for argument in command), flush=True)
    subprocess.run(command, cwd=ROOT, check=True)


def run_capture(command: list[str]) -> str:
    """Run one cook while retaining its deterministic optimization report."""
    print("+", " ".join(repr(argument) for argument in command), flush=True)
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, check=False)
    sys.stdout.write(result.stdout)
    sys.stderr.write(result.stderr)
    if result.returncode != 0:
        raise subprocess.CalledProcessError(result.returncode, command, result.stdout, result.stderr)
    return result.stdout


def source_hash(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def validate_asset_key(value: str) -> str:
    path = PurePosixPath(value)
    if (not value or len(value) > 4096 or path.is_absolute() or "\\" in value or
            "\n" in value or "\r" in value or "\0" in value or
            any(part in ("", ".", "..") for part in value.split("/"))):
        raise ValueError("asset path must be a safe project-relative path without `..`")
    return value


def parse_package(path: Path, expected_source: str, expected_hash: str) -> dict[str, str]:
    if not path.is_file() or path.stat().st_size > MAX_PACKAGE_BYTES:
        raise ValueError("cooked package is missing or exceeds the 64 MiB reader limit")
    fields: dict[str, str] = {}
    for line in path.read_text(encoding="ascii").splitlines():
        if len(line.encode("ascii")) > MAX_LINE_BYTES:
            raise ValueError("cooked package section exceeds the 16 MiB reader limit")
        key, separator, value = line.partition("=")
        if not separator or not key or key in fields:
            raise ValueError("cooked package has a malformed or duplicate section")
        fields[key] = value
    required = {
        "format", "source", "source_sha256", "triangles", "positions", "indices",
        "position_stride", "normal_stride", "uv_stride", "index_stride",
        "tangent_stride", "positions_b64", "normals_b64", "uvs_b64", "tangents_b64", "indices_b64",
    }
    if not required.issubset(fields):
        raise ValueError("cooked package is missing normalized geometry sections")
    if fields["format"] not in ("elisa-cooked-v2", "elisa-cooked-v3") or fields["source"] != expected_source:
        raise ValueError("cooked package format or source identity does not match")
    has_rig_format = fields["format"] == "elisa-cooked-v3"
    if fields["source_sha256"] != expected_hash:
        raise ValueError("cooked package source hash does not match the input FBX")
    if (fields["position_stride"], fields["normal_stride"], fields["uv_stride"],
            fields["tangent_stride"], fields["index_stride"]) != ("12", "12", "8", "16", "4"):
        raise ValueError("cooked package has unsupported geometry strides")

    def decode(name: str) -> bytes:
        try:
            return base64.b64decode(fields[name], validate=True)
        except (ValueError, binascii.Error) as failure:
            raise ValueError(f"cooked package has invalid base64 in {name}") from failure

    positions = decode("positions_b64")
    normals = decode("normals_b64")
    uvs = decode("uvs_b64")
    tangents = decode("tangents_b64")
    indices = decode("indices_b64")
    try:
        triangles = int(fields["triangles"])
        vertex_count = int(fields["positions"])
        index_count = int(fields["indices"])
    except ValueError as failure:
        raise ValueError("cooked package has invalid geometry counts") from failure
    if (triangles <= 0 or vertex_count <= 0 or index_count != triangles * 3 or
            len(positions) != vertex_count * 12 or len(normals) != vertex_count * 12 or
            len(uvs) != vertex_count * 8 or len(tangents) != vertex_count * 16 or
            len(indices) != index_count * 4):
        raise ValueError("cooked package geometry counts and byte lengths disagree")
    if not all(math.isfinite(value) for (value,) in struct.iter_unpack("<f", positions + normals + uvs + tangents)):
        raise ValueError("cooked package contains non-finite geometry")
    for vertex in range(vertex_count):
        tangent = struct.unpack_from("<4f", tangents, vertex * 16)
        normal = struct.unpack_from("<3f", normals, vertex * 12)
        tangent_length = math.sqrt(sum(component * component for component in tangent[:3]))
        alignment = sum(tangent[index] * normal[index] for index in range(3))
        if abs(tangent_length - 1.0) > 0.02 or abs(alignment) > 0.02 or abs(abs(tangent[3]) - 1.0) > 1e-4:
            raise ValueError("cooked package contains an invalid tangent frame")
    if any(value >= vertex_count for (value,) in struct.iter_unpack("<I", indices)):
        raise ValueError("cooked package contains an out-of-range index")
    subset_fields = {"material_slots", "subset_count", "subset_stride", "subsets_b64"}
    present_subset_fields = subset_fields.intersection(fields)
    if present_subset_fields and present_subset_fields != subset_fields:
        raise ValueError("cooked package has incomplete material subset metadata")
    if present_subset_fields:
        try:
            material_slots = int(fields["material_slots"])
            subset_count = int(fields["subset_count"])
        except ValueError as failure:
            raise ValueError("cooked package has invalid material subset counts") from failure
        subset_data = decode("subsets_b64")
        if not 1 <= material_slots <= 16 or not 1 <= subset_count <= 16 or \
                fields["subset_stride"] != "12" or len(subset_data) != subset_count * 12:
            raise ValueError("cooked package has unsupported material subset bounds or stride")
        subset_words = struct.unpack(f"<{subset_count * 3}I", subset_data)
        next_index = 0
        for subset in range(subset_count):
            start, count, slot = subset_words[subset * 3:subset * 3 + 3]
            if start != next_index or count == 0 or count % 3 or count > index_count - start or slot >= material_slots:
                raise ValueError("cooked package material subsets do not partition the index stream")
            next_index += count
        if next_index != index_count:
            raise ValueError("cooked package material subsets do not cover the index stream")
    skin_fields = {"skin_bones", "skin_indices_stride", "skin_weights_stride",
        "skin_indices_b64", "skin_weights_b64", "skin_names_b64"}
    present_skin_fields = skin_fields.intersection(fields)
    if present_skin_fields and present_skin_fields != skin_fields:
        raise ValueError("cooked package has an incomplete skin payload")
    if present_skin_fields:
        try:
            bone_count = int(fields["skin_bones"])
        except ValueError as failure:
            raise ValueError("cooked package has an invalid skin bone count") from failure
        if not 1 <= bone_count <= 64 or fields["skin_indices_stride"] != "16" or fields["skin_weights_stride"] != "16":
            raise ValueError("cooked package has unsupported skin bounds or strides")
        skin_indices = decode("skin_indices_b64")
        skin_weights = decode("skin_weights_b64")
        skin_names = decode("skin_names_b64")
        if len(skin_indices) != vertex_count * 16 or len(skin_weights) != vertex_count * 16:
            raise ValueError("cooked package skin influence lengths do not match the mesh")
        if not all(math.isfinite(value) for (value,) in struct.iter_unpack("<f", skin_weights)):
            raise ValueError("cooked package contains non-finite skin weights")
        for vertex in range(vertex_count):
            joint_indices = struct.unpack_from("<4I", skin_indices, vertex * 16)
            weights = struct.unpack_from("<4f", skin_weights, vertex * 16)
            if any(weight < 0.0 or (weight > 0.0 and joint >= bone_count)
                    for joint, weight in zip(joint_indices, weights)) or abs(sum(weights) - 1.0) > 0.005:
                raise ValueError("cooked package contains invalid or unnormalized skin influences")
        offset = 0
        for _ in range(bone_count):
            if len(skin_names) - offset < 4:
                raise ValueError("cooked package bone-name stream is truncated")
            (length,) = struct.unpack_from("<I", skin_names, offset)
            offset += 4
            if length > len(skin_names) - offset:
                raise ValueError("cooked package bone name exceeds its stream")
            offset += length
        if offset != len(skin_names):
            raise ValueError("cooked package bone-name stream has trailing bytes")

    rig_fields = {"skin_joints", "skin_joint_parent_stride", "skin_joint_rest_stride",
        "skin_joint_parents_b64", "skin_joint_rest_b64", "skin_joint_names_b64",
        "skin_cluster_joints_stride", "skin_cluster_joints_b64"}
    present_rig_fields = rig_fields.intersection(fields)
    if has_rig_format != bool(present_rig_fields) or (present_rig_fields and present_rig_fields != rig_fields) or \
            (present_rig_fields and not present_skin_fields):
        raise ValueError("cooked package has an incomplete rig hierarchy")
    if present_rig_fields:
        try:
            joint_count = int(fields["skin_joints"])
            cluster_count = int(fields["skin_bones"])
        except ValueError as failure:
            raise ValueError("cooked package has invalid rig counts") from failure
        if not 1 <= joint_count <= 64 or fields["skin_joint_parent_stride"] != "4" or \
                fields["skin_joint_rest_stride"] != "40" or fields["skin_cluster_joints_stride"] != "4":
            raise ValueError("cooked package has unsupported rig bounds or strides")
        parents = decode("skin_joint_parents_b64")
        rests = decode("skin_joint_rest_b64")
        names = decode("skin_joint_names_b64")
        cluster_joints = decode("skin_cluster_joints_b64")
        if len(parents) != joint_count * 4 or len(rests) != joint_count * 40 or \
                len(cluster_joints) != cluster_count * 4:
            raise ValueError("cooked package rig stream lengths do not match the hierarchy")

        def decode_names(data: bytes, count: int) -> list[bytes]:
            result: list[bytes] = []
            offset = 0
            for _ in range(count):
                if len(data) - offset < 4:
                    raise ValueError("cooked package joint-name stream is truncated")
                (length,) = struct.unpack_from("<I", data, offset)
                offset += 4
                if length > len(data) - offset:
                    raise ValueError("cooked package joint name exceeds its stream")
                result.append(data[offset:offset + length])
                offset += length
            if offset != len(data):
                raise ValueError("cooked package joint-name stream has trailing bytes")
            return result

        joint_names = decode_names(names, joint_count)
        skin_cluster_names = decode_names(skin_names, cluster_count)
        parent_values = struct.unpack(f"<{joint_count}i", parents)
        joint_palette = struct.unpack(f"<{cluster_count}I", cluster_joints)
        seen_clusters: set[int] = set()
        for joint_index, parent in enumerate(parent_values):
            if parent < -1 or parent >= joint_index:
                raise ValueError("cooked package rig is not parent ordered")
            rest = struct.unpack_from("<10f", rests, joint_index * 40)
            rotation_length = math.sqrt(sum(value * value for value in rest[3:7]))
            if not all(math.isfinite(value) for value in rest) or not 0.99 < rotation_length < 1.01 or \
                    any(value == 0.0 for value in rest[7:10]):
                raise ValueError("cooked package contains an invalid joint rest transform")
        for cluster_index, joint_index in enumerate(joint_palette):
            if joint_index >= joint_count or joint_index in seen_clusters or \
                    joint_names[joint_index] != skin_cluster_names[cluster_index]:
                raise ValueError("cooked package cluster map does not match its rig hierarchy")
            seen_clusters.add(joint_index)

        try:
            clip_count = int(fields["animation_clips"])
        except (KeyError, ValueError) as failure:
            raise ValueError("cooked package has an invalid animation clip count") from failure
        if not 0 <= clip_count <= cook_gltf_animation.MAX_CLIPS:
            raise ValueError("cooked package exceeds the animation clip limit")
        total_sample_floats = 0
        for clip_index in range(clip_count):
            prefix = f"animation_{clip_index}_"
            try:
                clip_name_data = decode(prefix + "name_b64")
                (name_length,) = struct.unpack_from("<I", clip_name_data)
                if name_length == 0 or name_length != len(clip_name_data) - 4:
                    raise ValueError("cooked package has an invalid animation name")
                duration = float(fields[prefix + "duration_seconds"])
                sample_rate = int(fields[prefix + "sample_rate"])
                frame_count = int(fields[prefix + "frames"])
                transform_stride = fields[prefix + "transform_stride"]
            except (KeyError, ValueError, struct.error) as failure:
                raise ValueError("cooked package has invalid animation metadata") from failure
            if not math.isfinite(duration) or duration <= 0.0 or not 1 <= sample_rate <= 120 or \
                    not 2 <= frame_count <= 3601 or transform_stride != "40":
                raise ValueError("cooked package has unsupported animation bounds or strides")
            sample_floats = joint_count * frame_count * 10
            if sample_floats > 2_000_000 - total_sample_floats:
                raise ValueError("cooked package exceeds the bounded animation sample budget")
            sample_data = decode(prefix + "samples_b64")
            if len(sample_data) != sample_floats * 4:
                raise ValueError("cooked package animation sample length is invalid")
            for offset in range(0, sample_floats, 10):
                sample = struct.unpack_from("<10f", sample_data, offset * 4)
                rotation_length = math.sqrt(sum(value * value for value in sample[3:7]))
                if not all(math.isfinite(value) for value in sample) or not 0.99 < rotation_length < 1.01 or \
                        any(value == 0.0 for value in sample[7:10]):
                    raise ValueError("cooked package contains an invalid animation sample")
            total_sample_floats += sample_floats
    return fields


def build_cooker(build_dir: Path) -> Path:
    dependency = ROOT / "dependencies/ufbx"
    meshoptimizer = ROOT / "dependencies/meshoptimizer"
    mikktspace = ROOT / "dependencies/mikktspace"
    source = dependency / "ufbx.c"
    header = dependency / "ufbx.h"
    if not source.is_file() or not header.is_file():
        raise ValueError("missing pinned ufbx files; run python3 scripts/fetch_dependencies.py")
    mikktspace_source = mikktspace / "mikktspace.c"
    mikktspace_header = mikktspace / "mikktspace.h"
    if not mikktspace_source.is_file() or not mikktspace_header.is_file():
        raise ValueError("missing pinned MikkTSpace files; run python3 scripts/fetch_dependencies.py")
    simplifier = meshoptimizer / "simplifier.cpp"
    vcache = meshoptimizer / "vcacheoptimizer.cpp"
    analyzer = meshoptimizer / "indexanalyzer.cpp"
    vfetch = meshoptimizer / "vfetchoptimizer.cpp"
    indexgenerator = meshoptimizer / "indexgenerator.cpp"
    if not all(path.is_file() for path in (meshoptimizer / "meshoptimizer.h", simplifier, vcache, analyzer, vfetch, indexgenerator)):
        raise ValueError("missing pinned meshoptimizer stages; run python3 scripts/fetch_dependencies.py")
    cc = os.environ.get("CC", "cc")
    cxx = os.environ.get("CXX", "c++")
    object_file = build_dir / "ufbx.o"
    mikktspace_object_file = build_dir / "mikktspace.o"
    executable = build_dir / "fbx-asset-cooker"
    run([cc, "-std=c99", "-O2", "-I", str(dependency), "-c", str(source), "-o", str(object_file)])
    run([cc, "-std=c99", "-O2", "-I", str(mikktspace), "-c", str(mikktspace_source),
        "-o", str(mikktspace_object_file)])
    run([cxx, "-std=c++17", "-O2", "-I", str(dependency), "-I", str(meshoptimizer),
        "-I", str(mikktspace), "-I", str(ROOT / "native"),
        str(ROOT / "native/fbx_asset_cooker.cpp"), str(simplifier), str(vcache), str(analyzer),
        str(vfetch), str(indexgenerator),
        str(meshoptimizer / "allocator.cpp"), str(object_file), str(mikktspace_object_file),
        "-o", str(executable)])
    return executable


def write_grid_fixture(path: Path, cells_per_side: int, two_materials: bool = False) -> None:
    """Write a small planar FBX grid that exercises the real simplification path."""
    vertex_count = (cells_per_side + 1) ** 2
    positions = []
    uvs = []
    for y in range(cells_per_side + 1):
        for x in range(cells_per_side + 1):
            positions.extend((str(x), str(y), "0"))
            uvs.extend((str(x / cells_per_side), str(y / cells_per_side)))
    polygon_indices = []
    material_assignments = []
    for y in range(cells_per_side):
        for x in range(cells_per_side):
            top_left = y * (cells_per_side + 1) + x
            top_right = top_left + 1
            bottom_left = top_left + cells_per_side + 1
            bottom_right = bottom_left + 1
            for triangle in ((top_left, top_right, bottom_right),
                    (top_left, bottom_right, bottom_left)):
                polygon_indices.extend(str(index) for index in triangle[:-1])
                polygon_indices.append(str(-triangle[-1] - 1))
                material_assignments.append(str(int(x >= cells_per_side // 2) if two_materials else 0))

    material_layer = ""
    material_layer_reference = ""
    material_objects = ""
    material_connections = ""
    material_definition = ""
    definition_count = 2
    if two_materials:
        definition_count = 4
        material_definition = 'ObjectType: "Material" { Count: 2 } '
        material_layer = (
            f'LayerElementMaterial: 0 {{ Version: 101 Name: "" MappingInformationType: "ByPolygon" '
            f'ReferenceInformationType: "IndexToDirect" Materials: *{len(material_assignments)} {{ a: '
            f'{",".join(material_assignments)} }} }} ')
        material_layer_reference = (
            ' LayerElement: { Type: "LayerElementMaterial" TypedIndex: 0 }')
        material_objects = (
            'Material: 1103, "Material::First", "" { Version: 102 } '
            'Material: 1104, "Material::Second", "" { Version: 102 } ')
        material_connections = ' C: "OO",1103,1002 C: "OO",1104,1002'

    path.write_text(
        '; FBX 7.4.0 project file\n'
        'FBXHeaderExtension: { FBXHeaderVersion: 1003 FBXVersion: 7400 }\n'
        'GlobalSettings: { Version: 1000 Properties70: { '
        'P: "UpAxis", "int", "Integer", "", 1 '
        'P: "UpAxisSign", "int", "Integer", "", 1 '
        'P: "FrontAxis", "int", "Integer", "", 2 '
        'P: "FrontAxisSign", "int", "Integer", "", 1 '
        'P: "CoordAxis", "int", "Integer", "", 0 '
        'P: "CoordAxisSign", "int", "Integer", "", 1 '
        'P: "UnitScaleFactor", "double", "Number", "", 1 } }\n'
        f'Definitions: {{ Version: 100 Count: {definition_count} '
        'ObjectType: "Geometry" { Count: 1 } ObjectType: "Model" { Count: 1 } '
        + material_definition + '}\n'
        'Objects: { '
        f'Geometry: 1001, "Geometry::Grid", "Mesh" {{ '
        f'GeometryVersion: 124 Vertices: *{vertex_count * 3} {{ a: '
        f'{",".join(positions)} }} '
        f'PolygonVertexIndex: *{len(polygon_indices)} {{ a: '
        f'{",".join(polygon_indices)} }} {material_layer}'
        f'LayerElementUV: 0 {{ Version: 101 Name: "UVMap" '
        f'MappingInformationType: "ByVertice" ReferenceInformationType: "Direct" '
        f'UV: *{len(uvs)} {{ a: {",".join(uvs)} }} }} '
        'Layer: 0 { Version: 100 LayerElement: { Type: "LayerElementUV" TypedIndex: 0 }'
        f'{material_layer_reference} }} }} '
        f'Model: 1002, "Model::Grid", "Mesh" {{ Version: 232 }} {material_objects}}}\n'
        f'Connections: {{ C: "OO",1001,1002 C: "OO",1002,0{material_connections} }}\n'
        'Takes: { Current: "" }\n',
        encoding="ascii")


def cook_one(cooker: Path, source: Path, asset_path: str, output: Path,
    max_triangles: int | None = None, mesh_name: str | None = None,
    report: list[str] | None = None) -> dict[str, str]:
    source = source.expanduser().resolve(strict=True)
    if not source.is_file():
        raise ValueError("FBX source must be a regular file")
    if source.stat().st_size == 0 or source.stat().st_size > MAX_FILE_BYTES:
        raise ValueError("FBX source is empty or exceeds the 512 MiB source limit")
    key = validate_asset_key(asset_path)
    digest = source_hash(source)
    output = output.expanduser().resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    command = [str(cooker), "--source", str(source), "--asset-path", key,
        "--output", str(output), "--sha256", digest]
    if max_triangles is not None:
        if isinstance(max_triangles, bool) or not 1 <= max_triangles <= 1000000:
            raise ValueError("max triangles must be an integer in [1, 1000000]")
        command.extend(["--max-triangles", str(max_triangles)])
    if mesh_name is not None:
        if not mesh_name or len(mesh_name.encode("utf-8")) > 512 or "\0" in mesh_name or "\n" in mesh_name or "\r" in mesh_name:
            raise ValueError("mesh name must be 1 to 512 UTF-8 bytes without line breaks")
        command.extend(["--mesh-name", mesh_name])
    output_text = run_capture(command)
    if report is not None:
        report.append(output_text)
    return parse_package(output, key, digest)


def main(arguments: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", nargs="?", type=Path, help="source FBX file")
    parser.add_argument("--asset-path", help="project-relative identity recorded in the cooked package")
    parser.add_argument("--output", type=Path, help="destination .pkg or .elpk path")
    parser.add_argument("--max-triangles", type=int, help="simplify output to no more than this many triangles")
    parser.add_argument("--mesh-name", help="select one FBX node or mesh by its exact name")
    parser.add_argument("--dependency", action="append", default=[], metavar="BUNDLE",
        help="name a bundle this .elpk needs, relative to the output's directory")
    parser.add_argument("--self-test", action="store_true", help="cook and validate the synthetic triangle fixture")
    options = parser.parse_args(arguments)
    if not options.self_test and (options.source is None or options.asset_path is None or options.output is None):
        parser.error("source, --asset-path, and --output are required unless --self-test is used")
    if options.self_test and (options.source is not None or options.asset_path is not None or options.output is not None or
            options.max_triangles is not None or options.mesh_name is not None):
        parser.error("--self-test cannot be combined with source, --asset-path, --output, or cooker options")
    if options.max_triangles is not None and not 1 <= options.max_triangles <= 1000000:
        parser.error("--max-triangles must be in [1, 1000000]")
    if options.dependency and (options.self_test or options.output.suffix.lower() != ".elpk"):
        parser.error("--dependency requires an .elpk output bundle")

    try:
        with tempfile.TemporaryDirectory(prefix="elisa-fbx-cooker-") as temporary:
            directory = Path(temporary)
            cooker = build_cooker(directory)
            if options.self_test:
                tangent_test = subprocess.run([sys.executable,
                    str(ROOT / "scripts/test_mikktspace.py")], check=False)
                if tangent_test.returncode != 0:
                    return tangent_test.returncode
                source = ROOT / "test/fixtures/fbx_triangle.fbx"
                output = directory / "triangle.pkg"
                fields = cook_one(cooker, source, "test/fixtures/fbx_triangle.fbx", output)
                if int(fields["triangles"]) != 1 or int(fields["positions"]) != 3:
                    raise ValueError("triangle fixture package counts do not match")
                multi_mesh_source = directory / "two-mesh-scene.fbx"
                write_two_mesh_scene(multi_mesh_source)
                multi_mesh_fields = cook_one(cooker, multi_mesh_source, "self-test/two-mesh-scene.fbx",
                    directory / "largest-mesh.pkg")
                selected_mesh_fields = cook_one(cooker, multi_mesh_source, "self-test/two-mesh-scene.fbx",
                    directory / "selected-mesh.pkg", mesh_name="SmallTriangle")
                if int(multi_mesh_fields["triangles"]) != 2 or int(selected_mesh_fields["triangles"]) != 1:
                    raise ValueError("exact FBX mesh-name selection did not override largest-mesh selection")
                material_source = directory / "two-material-mesh.fbx"
                write_two_material_mesh(material_source)
                material_fields = cook_one(cooker, material_source, "self-test/two-material-mesh.fbx",
                    directory / "two-material-mesh.pkg")
                material_subsets = base64.b64decode(material_fields["subsets_b64"], validate=True)
                if (int(material_fields["triangles"]) != 2 or material_fields.get("material_slots") != "2" or
                        material_fields.get("subset_count") != "2" or
                        struct.unpack("<6I", material_subsets) != (0, 3, 0, 3, 3, 1)):
                    raise ValueError("FBX cooker did not preserve its two polygon material subsets")
                try:
                    cook_one(cooker, material_source, "self-test/two-material-mesh.fbx",
                        directory / "under-budget.pkg", max_triangles=1)
                except subprocess.CalledProcessError as failure:
                    if "at least one triangle per FBX material subset" not in (failure.stderr or ""):
                        raise
                else:
                    raise ValueError("FBX simplification accepted fewer triangles than material subsets")
                grid_source = directory / "grid.fbx"
                cache_output = directory / "grid-cache.pkg"
                grid_output = directory / "grid.pkg"
                repeat_output = directory / "grid-repeat.pkg"
                cells_per_side = 16
                triangle_budget = 128
                write_grid_fixture(grid_source, cells_per_side)
                grid_key = "self-test/grid.fbx"
                cache_reports: list[str] = []
                cache_fields = cook_one(cooker, grid_source, grid_key, cache_output, report=cache_reports)
                grid_fields = cook_one(cooker, grid_source, grid_key, grid_output, triangle_budget)
                repeat_fields = cook_one(cooker, grid_source, grid_key, repeat_output, triangle_budget)
                original_triangles = cells_per_side * cells_per_side * 2
                if int(cache_fields["triangles"]) != original_triangles:
                    raise ValueError("cache optimization changed the triangle count")
                cache_report = re.search(r"meshoptimizer vertex cache: ACMR ([0-9.]+) -> ([0-9.]+)",
                    cache_reports[0])
                if cache_report is None or float(cache_report.group(2)) >= float(cache_report.group(1)):
                    raise ValueError("grid fixture did not improve its measured vertex-cache miss ratio")
                fetch_report = re.search(r"meshoptimizer vertex fetch: bytes fetched (\d+) -> (\d+) \(candidate (\d+)\), vertices (\d+) -> (\d+)",
                    cache_reports[0])
                if fetch_report is None or int(fetch_report.group(2)) > int(fetch_report.group(1)):
                    raise ValueError("grid fixture regressed its measured vertex-fetch cost")
                grid_triangles = int(grid_fields["triangles"])
                if not 0 < grid_triangles <= triangle_budget or grid_triangles >= original_triangles:
                    raise ValueError("grid fixture was not reduced to the requested triangle budget")
                if int(grid_fields["positions"]) >= (cells_per_side + 1) ** 2:
                    raise ValueError("simplified grid package retained unreferenced vertices")
                tangent_bytes = base64.b64decode(grid_fields["tangents_b64"], validate=True)
                for tangent in struct.iter_unpack("<4f", tangent_bytes):
                    if abs(tangent[0] - 1.0) > 0.02 or abs(tangent[1]) > 0.02 or \
                            abs(tangent[2]) > 0.02 or abs(tangent[3] - 1.0) > 0.02:
                        raise ValueError("planar UV fixture did not produce the expected +X tangent frame")
                if grid_fields != repeat_fields or grid_output.read_bytes() != repeat_output.read_bytes():
                    raise ValueError("simplified grid package output is not deterministic")
                material_grid_source = directory / "material-grid.fbx"
                write_grid_fixture(material_grid_source, 8, two_materials=True)
                material_grid_fields = cook_one(cooker, material_grid_source,
                    "self-test/material-grid.fbx", directory / "material-grid.pkg", max_triangles=64)
                material_grid_subsets = base64.b64decode(material_grid_fields["subsets_b64"], validate=True)
                subset_words = struct.unpack("<6I", material_grid_subsets)
                if (int(material_grid_fields["triangles"]) != 64 or
                        material_grid_fields.get("material_slots") != "2" or
                        material_grid_fields.get("subset_count") != "2" or
                        subset_words[0] != 0 or subset_words[2] != 0 or
                        subset_words[3] != subset_words[1] or subset_words[4] == 0 or subset_words[5] != 1 or
                        subset_words[1] + subset_words[4] != int(material_grid_fields["indices"])):
                    raise ValueError("simplification did not preserve the two material subset partitions")
                print(f"FBX cooker self-test passed: mesh selection and material subsets plus {original_triangles} -> "
                    f"{grid_triangles} deterministic simplified grid triangles; vertex-cache ACMR "
                    f"{cache_report.group(1)} -> {cache_report.group(2)}; vertex-fetch bytes "
                    f"{fetch_report.group(1)} -> {fetch_report.group(2)}")
            else:
                package_output = options.output
                geometry_output = directory / "geometry.pkg" if package_output.suffix.lower() == ".elpk" else package_output
                fields = cook_one(cooker, options.source, options.asset_path, geometry_output,
                    options.max_triangles, options.mesh_name)
                if package_output.suffix.lower() == ".elpk":
                    write_geometry_package(package_output, geometry_output.read_bytes(),
                        dependencies=options.dependency)
                print(f"FBX package validated: {fields['triangles']} triangles, {fields['positions']} vertices")
    except (OSError, RuntimeError, ValueError, subprocess.CalledProcessError) as failure:
        print(f"FBX cooking failed: {failure}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
