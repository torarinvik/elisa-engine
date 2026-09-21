#!/usr/bin/env python3
"""Cook one bounded, static glTF triangle mesh into Elisa's runtime package."""

from __future__ import annotations

import argparse
import base64
from copy import deepcopy
from pathlib import Path
import struct
import sys
import tempfile

import cook_assets
import cook_gltf_geometry
from elisa_package import parse_texture_arguments, write_geometry_package
from png_image import encode_png


ROOT = Path(__file__).resolve().parents[1]


def self_test() -> int:
    source = ROOT / "examples/maze/assets/maze_tile.gltf"
    with tempfile.TemporaryDirectory(prefix="elisa-gltf-cooker-") as temporary:
        first = Path(temporary) / "first.pkg"
        second = Path(temporary) / "second.pkg"
        first_path, first_result = cook_assets.cook_geometry_package(
            source, "assets/maze_tile.gltf", first)
        second_path, second_result = cook_assets.cook_geometry_package(
            source, "assets/maze_tile.gltf", second)
        if (first_result != second_result or first_path.read_bytes() != second_path.read_bytes() or
                first_result["triangles"] != 12 or first_result["positions"] != 24 or
                first_result["indices"] != 36):
            print("glTF cooker self-test failed: output was not stable or counts differ", file=sys.stderr)
            return 1
        sections = dict(line.split("=", 1) for line in first_path.read_text(encoding="utf-8").splitlines())
        if (sections.get("format") != "elisa-cooked-v2" or
                sections.get("normal_stride") != "12" or sections.get("uv_stride") != "8" or
                sections.get("index_stride") != "4"):
            print("glTF cooker self-test failed: runtime streams are incomplete", file=sys.stderr)
            return 1
        document = cook_assets.read_gltf(source.read_bytes())
        buffer = cook_assets.source_bytes(source.parent, document)
        rejected = []
        transformed = deepcopy(document)
        transformed["nodes"][0]["translation"] = [1.0, 0.0, 0.0]
        rejected.append(("node transform", transformed))
        skinned = deepcopy(document)
        skinned["skins"] = [{}]
        rejected.append(("skin", skinned))
        extra_attribute = deepcopy(document)
        extra_attribute["meshes"][0]["primitives"][0]["attributes"]["JOINTS_0"] = 1
        rejected.append(("unsupported vertex attribute", extra_attribute))
        morphed = deepcopy(document)
        morphed["meshes"][0]["primitives"][0]["targets"] = [{}]
        rejected.append(("morph target", morphed))
        material_bound = deepcopy(document)
        material_bound["meshes"][0]["primitives"][0]["material"] = 0
        rejected.append(("material index outside the document", material_bound))
        multi_node = deepcopy(document)
        multi_node["nodes"].append({"mesh": 0})
        multi_node["scenes"][0]["nodes"].append(1)
        rejected.append(("multiple mesh nodes", multi_node))
        for label, unsupported in rejected:
            try:
                cook_gltf_geometry.normalized_geometry(unsupported, buffer)
            except (ValueError, KeyError, IndexError, TypeError):
                continue
            print(f"glTF cooker self-test failed: accepted unsupported {label}", file=sys.stderr)
            return 1
        texture_status = texture_self_test(source, Path(temporary))
        if texture_status != 0:
            return texture_status
        subset_status = subset_self_test(Path(temporary))
        if subset_status != 0:
            return subset_status
    print("glTF cooker self-test passed: deterministic 12-triangle runtime package with image sections "
        "and a three-subset, two-slot panel")
    return 0


def subset_self_test(temporary: Path) -> int:
    """Cook the multi-material panel: three primitives, two material slots,
    the outer strips sharing one vertex block."""
    source = ROOT / "test/fixtures/multi_material_panel.gltf"
    cooked = []
    for label in ("first", "second"):
        path, result = cook_gltf_geometry.cook_geometry_package(
            source, "test/fixtures/multi_material_panel.gltf", temporary / f"panel-{label}.pkg")
        cooked.append((path.read_bytes(), result))
    sections = dict(line.split("=", 1) for line in cooked[0][0].decode("utf-8").splitlines())
    if (cooked[0] != cooked[1] or cooked[0][1]["positions"] != 12 or cooked[0][1]["indices"] != 18 or
            sections.get("material_slots") != "2" or sections.get("subset_count") != "3" or
            base64.b64decode(sections.get("subsets_b64", "")) !=
            struct.pack("<9I", 0, 6, 1, 6, 6, 0, 12, 6, 1)):
        print("glTF cooker self-test failed: the panel's subsets are unstable or wrong", file=sys.stderr)
        return 1
    document = cook_assets.read_gltf(source.read_bytes())
    buffer = cook_assets.source_bytes(source.parent, document)
    variants = {
        "a primitive without a material beside bound ones":
            lambda d: d["meshes"][0]["primitives"][1].pop("material"),
        "a material index outside the document": lambda d: d["meshes"][0]["primitives"][2].update(material=2),
        "a material with properties": lambda d: d["materials"][0].update(doubleSided=True),
        "17 materials": lambda d: d["materials"].extend({"name": "extra"} for _ in range(15)),
        "17 primitives": lambda d: d["meshes"][0]["primitives"].extend(
            deepcopy(d["meshes"][0]["primitives"][1]) for _ in range(14)),
        "a line primitive": lambda d: d["meshes"][0]["primitives"][1].update(mode=1),
        "shared positions with different attributes":
            lambda d: d["meshes"][0]["primitives"][2]["attributes"].pop("NORMAL"),
        "attributes that are not an accessor map": lambda d: d["meshes"][0]["primitives"][1].update(attributes=[0]),
        "an attribute naming an accessor by list": lambda d: d["meshes"][0]["primitives"][1].update(
            attributes={"POSITION": [3]}),
    }
    for label, mutate in variants.items():
        variant = deepcopy(document)
        mutate(variant)
        try:
            cook_gltf_geometry.normalized_geometry(variant, buffer)
        except ValueError:
            continue
        print(f"glTF cooker self-test failed: accepted {label}", file=sys.stderr)
        return 1
    return 0


def texture_self_test(source: Path, temporary: Path) -> int:
    image = temporary / "albedo.png"
    image.write_bytes(encode_png(2, 1, bytes([255, 0, 0, 255, 0, 0, 255, 255])))
    oversized = temporary / "oversized.png"
    # A valid PNG header that claims 65535x1 pixels and has no image data.
    oversized.write_bytes(encode_png(1, 1, bytes(4))[:16] + (65535).to_bytes(4, "big") +
        encode_png(1, 1, bytes(4))[20:])
    not_image = temporary / "not-image.png"
    not_image.write_bytes(b"not an image")
    bundles = []
    for label in ("first", "second"):
        bundle = temporary / f"{label}.elpk"
        if main([str(source), "--asset-path", "assets/maze_tile.gltf", "--output", str(bundle),
                "--texture", f"albedo={image}"]) != 0:
            print("glTF cooker self-test failed: a PNG texture section was rejected", file=sys.stderr)
            return 1
        bundles.append(bundle.read_bytes())
    if bundles[0] != bundles[1] or b"albedo" not in bundles[0]:
        print("glTF cooker self-test failed: texture bundles differ or lack the section", file=sys.stderr)
        return 1
    dependent = temporary / "dependent.elpk"
    if main([str(source), "--asset-path", "assets/maze_tile.gltf", "--output", str(dependent),
            "--dependency", "textures/wall.elpk", "--dependency", "shared.elpk"]) != 0 or \
            b"dependency=shared.elpk\ndependency=textures/wall.elpk\n" not in dependent.read_bytes():
        print("glTF cooker self-test failed: dependencies were not written in sorted order", file=sys.stderr)
        return 1
    rejected = [
        ("oversized image header", ["--texture", f"albedo={oversized}"], ".elpk"),
        ("non-image section", ["--texture", f"albedo={not_image}"], ".elpk"),
        ("reserved mesh section", ["--texture", f"mesh={image}"], ".elpk"),
        ("unsafe section name", ["--texture", f"Albedo={image}"], ".elpk"),
        ("duplicate section", ["--texture", f"albedo={image}", "--texture", f"albedo={image}"], ".elpk"),
        ("texture without a bundle", ["--texture", f"albedo={image}"], ".pkg"),
        ("dependency without a bundle", ["--dependency", "shared.elpk"], ".pkg"),
        ("escaping dependency", ["--dependency", "../shared.elpk"], ".elpk"),
        ("absolute dependency", ["--dependency", "/shared.elpk"], ".elpk"),
        ("duplicate dependency", ["--dependency", "shared.elpk", "--dependency", "shared.elpk"], ".elpk"),
    ]
    for label, flags, suffix in rejected:
        arguments = [str(source), "--asset-path", "assets/maze_tile.gltf",
            "--output", str(temporary / f"rejected{suffix}"), *flags]
        try:
            status = main(arguments)
        except SystemExit as exit_request:
            status = exit_request.code
        if status == 0:
            print(f"glTF cooker self-test failed: accepted {label}", file=sys.stderr)
            return 1
    return 0


def main(arguments: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", nargs="?", type=Path, help="source glTF file")
    parser.add_argument("--asset-path", help="safe project-relative source identity")
    parser.add_argument("--output", type=Path, help="destination runtime package")
    parser.add_argument("--texture", action="append", default=[], metavar="SECTION=PATH",
        help="add a PNG or JPEG image as a named section of an .elpk bundle")
    parser.add_argument("--dependency", action="append", default=[], metavar="BUNDLE",
        help="name a bundle this .elpk needs, relative to the output's directory")
    parser.add_argument("--self-test", action="store_true", help="cook the authored maze mesh twice")
    options = parser.parse_args(arguments)
    if options.self_test:
        if options.source is not None or options.asset_path is not None or options.output is not None:
            parser.error("--self-test cannot be combined with source, --asset-path, or --output")
        return self_test()
    if options.source is None or options.asset_path is None or options.output is None:
        parser.error("source, --asset-path, and --output are required")
    try:
        textures = parse_texture_arguments(options.texture)
        if (textures or options.dependency) and options.output.suffix.lower() != ".elpk":
            raise ValueError("--texture and --dependency require an .elpk output bundle")
        if options.output.suffix.lower() == ".elpk":
            images = {name: path.read_bytes() for name, path in textures.items()}
            with tempfile.TemporaryDirectory(prefix="elisa-gltf-bundle-") as temporary:
                geometry_path, result = cook_assets.cook_geometry_package(
                    options.source, options.asset_path, Path(temporary) / "geometry.pkg")
                write_geometry_package(options.output, geometry_path.read_bytes(), images,
                    options.dependency)
            output = options.output.expanduser().resolve()
        else:
            output, result = cook_assets.cook_geometry_package(
                options.source, options.asset_path, options.output)
        print(f"cooked {options.asset_path} -> {output} "
            f"({result['triangles']} triangles, {result['positions']} vertices)")
    except (OSError, RuntimeError, ValueError, KeyError, IndexError, TypeError, AttributeError) as failure:
        print(f"glTF cooking failed: {failure}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
