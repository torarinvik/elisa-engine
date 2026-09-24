#!/usr/bin/env python3
"""Cook one bounded FBX mesh into Elisa's existing cooked geometry package."""

from __future__ import annotations

import argparse
import base64
import hashlib
import math
import os
from pathlib import Path, PurePosixPath
import re
import struct
import subprocess
import sys
import tempfile

from fbx_cooked_package import MAX_SOURCE_MESH_COUNT, package_fbx_texture_sources, parse_package
from fbx_material_cooker_self_test import validate_material_package
from elisa_package import write_geometry_package
from meshopt_cooked_package import compress_core_geometry
from fbx_surface_texture import decode_png_rgba
from fbx_surface_texture_self_test import validate_surface_texture_decoder
from fbx_test_fixtures import write_two_material_mesh, write_two_mesh_scene
from png_image import encode_png


ROOT = Path(__file__).resolve().parents[1]
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
    report: list[str] | None = None, ignore_material_textures: bool = False,
    all_meshes: bool = False) -> dict[str, str]:
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
    if ignore_material_textures:
        command.append("--ignore-material-textures")
    if all_meshes:
        if mesh_name is not None:
            raise ValueError("all meshes cannot be combined with an exact mesh selector")
        command.append("--all-meshes")
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
    parser.add_argument("--ignore-material-textures", action="store_true",
        help="ignore FBX image references when the application supplies textures separately")
    parser.add_argument("--all-meshes", action="store_true",
        help="combine all static triangle meshes in the FBX scene into one cooked mesh")
    parser.add_argument("--dependency", action="append", default=[], metavar="BUNDLE",
        help="name a bundle this .elpk needs, relative to the output's directory")
    parser.add_argument("--self-test", action="store_true", help="cook and validate the synthetic triangle fixture")
    options = parser.parse_args(arguments)
    if not options.self_test and (options.source is None or options.asset_path is None or options.output is None):
        parser.error("source, --asset-path, and --output are required unless --self-test is used")
    if options.self_test and (options.source is not None or options.asset_path is not None or options.output is not None or
            options.max_triangles is not None or options.mesh_name is not None or options.all_meshes or
            options.ignore_material_textures):
        parser.error("--self-test cannot be combined with source, --asset-path, --output, or cooker options")
    if options.max_triangles is not None and not 1 <= options.max_triangles <= 1000000:
        parser.error("--max-triangles must be in [1, 1000000]")
    if options.all_meshes and options.mesh_name is not None:
        parser.error("--all-meshes cannot be combined with --mesh-name")
    if options.dependency and (options.self_test or options.output.suffix.lower() != ".elpk"):
        parser.error("--dependency requires an .elpk output bundle")

    try:
        with tempfile.TemporaryDirectory(prefix="elisa-fbx-cooker-") as temporary:
            directory = Path(temporary)
            cooker = build_cooker(directory)
            if options.self_test:
                validate_surface_texture_decoder()
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
                all_meshes_package = directory / "all-meshes.pkg"
                combined_mesh_fields = cook_one(cooker, multi_mesh_source, "self-test/two-mesh-scene.fbx",
                    all_meshes_package, all_meshes=True)
                if (int(multi_mesh_fields["triangles"]) != 2 or
                        multi_mesh_fields.get("source_mesh_count") != "1" or
                        int(selected_mesh_fields["triangles"]) != 1 or
                        selected_mesh_fields.get("source_mesh_count") != "1"):
                    raise ValueError("exact FBX mesh-name selection did not override largest-mesh selection")
                combined_subsets = base64.b64decode(combined_mesh_fields["subsets_b64"], validate=True)
                combined_indices = struct.unpack("<9I",
                    base64.b64decode(combined_mesh_fields["indices_b64"], validate=True))
                combined_placements = list(struct.iter_unpack("<8I12f",
                    base64.b64decode(combined_mesh_fields.get("mesh_placements_b64", ""), validate=True)))
                if (int(combined_mesh_fields["triangles"]) != 3 or
                        int(combined_mesh_fields["positions"]) != 7 or
                        combined_mesh_fields.get("source_mesh_count") != "2" or
                        combined_mesh_fields.get("material_slots") != "2" or
                        combined_mesh_fields.get("subset_count") != "2" or
                        struct.unpack("<6I", combined_subsets) != (0, 3, 0, 3, 6, 1) or
                        not all(index < 3 for index in combined_indices[:3]) or
                        not all(3 <= index < 7 for index in combined_indices[3:]) or
                        combined_mesh_fields.get("mesh_count") != "2" or
                        combined_mesh_fields.get("mesh_placement_count") != "2" or
                        combined_mesh_fields.get("mesh_placement_stride") != "80" or
                        len(combined_placements) != 2 or
                        combined_placements[0][:8] != (0, combined_placements[0][1], 0, 3, 0, 3, 0, 1) or
                        combined_placements[1][:8] != (1, combined_placements[1][1], 3, 4, 3, 6, 1, 1) or
                        combined_placements[0][1] == combined_placements[1][1] or
                        any(not math.isfinite(value) for placement in combined_placements for value in placement[8:]) or
                        combined_placements[0][8:] == combined_placements[1][8:]):
                    raise ValueError("FBX all-mesh cooking did not preserve source placements and ranges")
                valid_package = all_meshes_package.read_text(encoding="ascii")
                for invalid_count in (0, MAX_SOURCE_MESH_COUNT + 1):
                    invalid_package = directory / f"invalid-source-mesh-count-{invalid_count}.pkg"
                    invalid_package.write_text(valid_package.replace(
                        "source_mesh_count=2", f"source_mesh_count={invalid_count}"), encoding="ascii")
                    try:
                        parse_package(invalid_package, "self-test/two-mesh-scene.fbx",
                            source_hash(multi_mesh_source))
                    except ValueError as failure:
                        if "source mesh count" not in str(failure):
                            raise
                    else:
                        raise ValueError("FBX package reader accepted an invalid source-mesh count")
                material_source = directory / "two-material-mesh.fbx"
                write_two_material_mesh(material_source)
                material_fields = cook_one(cooker, material_source, "self-test/two-material-mesh.fbx",
                    directory / "two-material-mesh.pkg")
                material_subsets = base64.b64decode(material_fields["subsets_b64"], validate=True)
                if (int(material_fields["triangles"]) != 2 or material_fields.get("material_slots") != "2" or
                        material_fields.get("subset_count") != "2" or
                        struct.unpack("<6I", material_subsets) != (0, 3, 0, 3, 3, 1)):
                    raise ValueError("FBX cooker did not preserve its two polygon material subsets")
                validate_material_package(material_fields)
                texture_directory = directory / "textured-fbx"
                texture_directory.mkdir()
                texture_source = texture_directory / "two-material-texture.fbx"
                (texture_directory / "albedo.png").write_bytes(encode_png(2, 1,
                    bytes((220, 80, 40, 255, 40, 80, 220, 255))))
                write_two_material_mesh(texture_source, "albedo.png")
                texture_fields = cook_one(cooker, texture_source,
                    "self-test/textured-fbx/two-material-texture.fbx",
                    directory / "textured-fbx-geometry.pkg")
                expected_refs = struct.pack("<10I", 1, 0, 0, 0, 0, 0, 0, 0, 0, 0)
                if (texture_fields.get("texture_source_count") != "1" or
                        base64.b64decode(texture_fields["slot_texture_sources_b64"], validate=True) != expected_refs):
                    raise ValueError("FBX cooker did not retain its base-color texture binding")
                bundled_geometry, bundled_images = package_fbx_texture_sources(
                    texture_source, (directory / "textured-fbx-geometry.pkg").read_bytes(), texture_fields)
                textured_runtime_fields = dict(line.split("=", 1)
                    for line in bundled_geometry.decode("ascii").splitlines())
                textured_runtime_materials = list(struct.iter_unpack("<10f2I", base64.b64decode(
                    textured_runtime_fields["slot_materials_b64"], validate=True)))
                if (list(bundled_images) != ["fbx_image_0"] or
                        bundled_images["fbx_image_0"] != (texture_directory / "albedo.png").read_bytes() or
                        b"texture_source_" in bundled_geometry or b"texture_count=1" not in bundled_geometry or
                        base64.b64decode(textured_runtime_fields["slot_textures_b64"], validate=True) != expected_refs or
                        textured_runtime_materials[0][10] != 2):
                    raise ValueError("FBX external texture was not converted to a packaged image slot")
                write_geometry_package(directory / "textured-fbx.elpk", bundled_geometry, bundled_images)
                ignored_texture_fields = cook_one(cooker, texture_source,
                    "self-test/textured-fbx/two-material-texture.fbx",
                    directory / "textured-fbx-ignored.pkg", ignore_material_textures=True)
                validate_material_package(ignored_texture_fields)
                if "texture_source_count" in ignored_texture_fields:
                    raise ValueError("explicitly ignored FBX textures leaked into the cooked package")


                cutout_directory = directory / "cutout-fbx"
                cutout_directory.mkdir()
                cutout_source = cutout_directory / "two-material-cutout.fbx"
                cutout_image = encode_png(2, 1,
                    bytes((220, 80, 40, 0, 40, 80, 220, 255)))
                (cutout_directory / "cutout.png").write_bytes(cutout_image)
                write_two_material_mesh(cutout_source, "cutout.png", transparency_factor=0.0)
                cutout_fields = cook_one(cooker, cutout_source,
                    "self-test/cutout-fbx/two-material-cutout.fbx",
                    directory / "cutout-fbx-geometry.pkg")
                raw_materials = list(struct.iter_unpack("<10f2I", base64.b64decode(
                    cutout_fields["slot_materials_b64"], validate=True)))
                if raw_materials[0][10] != 0:
                    raise ValueError("opaque FBX alpha factors were classified before reading their base-color image")
                bundled_cutout, cutout_images = package_fbx_texture_sources(
                    cutout_source, (directory / "cutout-fbx-geometry.pkg").read_bytes(), cutout_fields)
                cutout_runtime_fields = dict(line.split("=", 1)
                    for line in bundled_cutout.decode("ascii").splitlines())
                cutout_materials = list(struct.iter_unpack("<10f2I", base64.b64decode(
                    cutout_runtime_fields["slot_materials_b64"], validate=True)))
                if (cutout_materials[0][10] != 1 or cutout_materials[0][9] != 0.5 or
                        cutout_materials[1][10] != 0 or cutout_images != {"fbx_image_0": cutout_image}):
                    raise ValueError("binary FBX base-color alpha did not become a masked runtime material")
                write_geometry_package(directory / "cutout-fbx.elpk", bundled_cutout, cutout_images)

                surface_directory = directory / "surface-textured-fbx"
                surface_directory.mkdir()
                surface_source = surface_directory / "two-material-surface-texture.fbx"
                roughness_pixels = bytes((64, 1, 2, 255, 128, 3, 4, 255))
                metalness_pixels = bytes((200, 5, 6, 255, 20, 7, 8, 255))
                roughness_path = surface_directory / "roughness.png"
                metalness_path = surface_directory / "metalness.png"
                roughness_path.write_bytes(encode_png(2, 1, roughness_pixels))
                metalness_path.write_bytes(encode_png(2, 1, metalness_pixels))
                write_two_material_mesh(surface_source,
                    roughness_texture="roughness.png", metalness_texture="metalness.png")
                surface_fields = cook_one(cooker, surface_source,
                    "self-test/surface-textured-fbx/two-material-surface-texture.fbx",
                    directory / "surface-textured-fbx-geometry.pkg")
                if (surface_fields.get("texture_source_count") != "2" or
                        struct.unpack("<4I", base64.b64decode(
                            surface_fields["slot_surface_texture_sources_b64"], validate=True)) != (1, 2, 0, 0) or
                        struct.unpack("<10I", base64.b64decode(
                            surface_fields["slot_texture_sources_b64"], validate=True)) != (0,) * 10):
                    raise ValueError("FBX cooker did not preserve separate roughness and metalness source roles")
                bundled_surface_geometry, bundled_surface_images = package_fbx_texture_sources(
                    surface_source, (directory / "surface-textured-fbx-geometry.pkg").read_bytes(), surface_fields)
                runtime_surface_fields = dict(line.split("=", 1)
                    for line in bundled_surface_geometry.decode("ascii").splitlines())
                packed_image = bundled_surface_images.get("fbx_image_0")
                if packed_image is None or len(bundled_surface_images) != 1 or \
                        runtime_surface_fields.get("texture_count") != "1" or \
                        struct.unpack("<10I", base64.b64decode(
                            runtime_surface_fields["slot_textures_b64"], validate=True)) != (0, 0, 1, 0, 0, 0, 0, 0, 0, 0):
                    raise ValueError("separate FBX surface maps did not become one runtime surface slot")
                packed_width, packed_height, packed_rgba = decode_png_rgba(packed_image)
                if (packed_width, packed_height, packed_rgba) != (2, 1,
                        bytes((255, 64, 200, 255, 255, 128, 20, 255))):
                    raise ValueError("packed FBX surface image does not hold roughness in G and metalness in B")
                write_geometry_package(directory / "surface-textured-fbx.elpk",
                    bundled_surface_geometry, bundled_surface_images)
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
                compressed_grid, stream_report = compress_core_geometry(grid_output.read_bytes())
                repeated_compressed_grid, repeated_stream_report = compress_core_geometry(
                    repeat_output.read_bytes())
                compressed_grid_fields = dict(line.split("=", 1)
                    for line in compressed_grid.decode("ascii").splitlines())
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
                if (compressed_grid != repeated_compressed_grid or stream_report != repeated_stream_report or
                        stream_report["compressed_streams"] == 0 or
                        stream_report["stored_bytes"] >= stream_report["raw_bytes"] or
                        compressed_grid_fields.get("meshopt_codec") != "meshoptimizer-v1.2" or
                        compressed_grid_fields.get("positions") != grid_fields.get("positions") or
                        compressed_grid_fields.get("triangles") != grid_fields.get("triangles") or
                        int(compressed_grid_fields.get("indices", "0")) != grid_triangles * 3):
                    raise ValueError("meshoptimizer FBX stream compression is not deterministic or lost counts")
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
                package_output = options.output.expanduser().resolve()
                geometry_output = directory / "geometry.pkg"
                fields = cook_one(cooker, options.source, options.asset_path, geometry_output,
                    options.max_triangles, options.mesh_name,
                    ignore_material_textures=options.ignore_material_textures,
                    all_meshes=options.all_meshes)
                if package_output.suffix.lower() == ".elpk":
                    geometry, images = package_fbx_texture_sources(
                        options.source.expanduser().resolve(strict=True), geometry_output.read_bytes(), fields)
                    geometry, stream_report = compress_core_geometry(geometry)
                    write_geometry_package(package_output, geometry, images,
                        dependencies=options.dependency)
                elif "texture_source_count" in fields:
                    raise ValueError("FBX external material textures require an .elpk output bundle")
                else:
                    geometry, stream_report = compress_core_geometry(geometry_output.read_bytes())
                    package_output.parent.mkdir(parents=True, exist_ok=True)
                    temporary_output = package_output.with_name(package_output.name + ".tmp")
                    try:
                        temporary_output.write_bytes(geometry)
                        os.replace(temporary_output, package_output)
                    finally:
                        temporary_output.unlink(missing_ok=True)
                print(f"FBX package validated: {fields['triangles']} triangles, {fields['positions']} vertices")
                print(f"meshoptimizer streams: {stream_report['raw_bytes']} -> "
                    f"{stream_report['stored_bytes']} bytes "
                    f"({stream_report['compressed_streams']} streams compressed)")
    except (OSError, RuntimeError, ValueError, subprocess.CalledProcessError) as failure:
        print(f"FBX cooking failed: {failure}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
