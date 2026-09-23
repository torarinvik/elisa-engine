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
from cook_gltf_meshopt import self_test as meshopt_self_test
from cook_gltf_lod import self_test as lod_self_test
from cook_gltf_lod_package import cook_lod_chain, parse_lod_ratios
from cook_gltf_lod_package import self_test as lod_package_self_test
from cook_gltf_mikktspace import self_test as mikktspace_self_test
from elisa_package import parse_texture_arguments, write_geometry_package
from gltf_hierarchy_self_test import hierarchy_self_test
from gltf_morph_self_test import self_test as morph_self_test
from gltf_scene_self_test import self_test as scene_self_test
from gltf_skin_self_test import self_test as skin_self_test
from gltf_texture_self_test import material_texture_self_test
from png_image import encode_png


ROOT = Path(__file__).resolve().parents[1]


def self_test() -> int:
    mikktspace_status = mikktspace_self_test()
    if mikktspace_status != 0:
        return mikktspace_status
    meshopt_status = meshopt_self_test()
    if meshopt_status != 0:
        return meshopt_status
    lod_status = lod_self_test()
    if lod_status != 0:
        return lod_status
    lod_package_status = lod_package_self_test()
    if lod_package_status != 0:
        return lod_package_status
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
        camera_node = deepcopy(document)
        camera_node["nodes"][0]["camera"] = 0
        rejected.append(("camera node", camera_node))
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
        two_scenes = deepcopy(document)
        two_scenes["scenes"].append({"nodes": [0]})
        rejected.append(("second scene", two_scenes))
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
        hierarchy_status = hierarchy_self_test(Path(temporary))
        if hierarchy_status != 0:
            return hierarchy_status
        skin_status = skin_self_test(Path(temporary))
        if skin_status != 0:
            return skin_status
        morph_status = morph_self_test(Path(temporary))
        if morph_status != 0:
            return morph_status
        scene_status = scene_self_test(Path(temporary))
        if scene_status != 0:
            return scene_status
        material_texture_status = material_texture_self_test(Path(temporary), main)
        if material_texture_status != 0:
            return material_texture_status
    print("glTF cooker self-test passed: deterministic 12-triangle runtime package with image sections, "
        "a three-subset, two-slot panel with authored slot materials, a baked node hierarchy, "
        "and bundled PNG/KTX2 material textures")
    return 0


# The panel fixture's authored slot materials, packed as the cooker packs them.
PANEL_SLOT_MATERIALS = (
    struct.pack("<10f2I", 0.0, 0.0, 0.08, 1.0, 0.0, 0.9, 0.0, 0.0, 1.0, 0.5, 0, 1) +
    struct.pack("<10f2I", 0.08, 0.0, 0.0, 1.0, 0.25, 0.75, 1.0, 0.0, 0.0, 0.5, 0, 1))
GLTF_DEFAULT_SLOT_MATERIAL = struct.pack("<10f2I", 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 0.0, 0.0, 0.0, 0.5, 0, 0)
GLASS_SLOT_MATERIAL = struct.pack("<10f2I", 0.0, 0.0, 0.08, 0.5, 0.0, 0.9, 0.0, 0.0, 1.0, 0.5, 2, 0)


def make_glass(document: dict) -> None:
    """Turn the panel's center slot into blended, single-sided glass."""
    center = document["materials"][0]
    center["alphaMode"] = "BLEND"
    center["pbrMetallicRoughness"]["baseColorFactor"][3] = 0.5
    center["doubleSided"] = False


def single_slot(document: dict) -> None:
    """Keep only the first strip, bound to the only material."""
    document["meshes"][0]["primitives"] = document["meshes"][0]["primitives"][:1]
    document["meshes"][0]["primitives"][0]["material"] = 0
    document["materials"] = document["materials"][:1]


def subset_self_test(temporary: Path) -> int:
    """Cook the multi-material panel: three primitives, two material slots,
    the outer strips sharing one vertex block, each slot with its authored
    factors."""
    source = ROOT / "test/fixtures/multi_material_panel.gltf"
    cooked = []
    for label in ("first", "second"):
        path, result = cook_gltf_geometry.cook_geometry_package(
            source, "test/fixtures/multi_material_panel.gltf", temporary / f"panel-{label}.pkg")
        cooked.append((path.read_bytes(), result))
    sections = dict(line.split("=", 1) for line in cooked[0][0].decode("utf-8").splitlines())
    if (cooked[0] != cooked[1] or cooked[0][1]["positions"] != 12 or cooked[0][1]["indices"] != 18 or
            cooked[0][1]["slot_materials"] != 2 or
            sections.get("material_slots") != "2" or sections.get("subset_count") != "3" or
            base64.b64decode(sections.get("subsets_b64", "")) !=
            struct.pack("<9I", 0, 6, 1, 6, 6, 0, 12, 6, 1) or
            sections.get("slot_material_stride") != "48" or
            base64.b64decode(sections.get("slot_materials_b64", "")) != PANEL_SLOT_MATERIALS):
        print("glTF cooker self-test failed: the panel's subsets or slot materials are unstable or wrong",
            file=sys.stderr)
        return 1
    document = cook_assets.read_gltf(source.read_bytes())
    buffer = cook_assets.source_bytes(source.parent, document)
    accepted = {
        "a name-only material cooks the glTF defaults": (lambda d: d["materials"].__setitem__(0, {"name": "center"}),
            GLTF_DEFAULT_SLOT_MATERIAL + PANEL_SLOT_MATERIALS[48:]),
        "a blended single-sided slot": (make_glass, GLASS_SLOT_MATERIAL + PANEL_SLOT_MATERIALS[48:]),
        "one primitive with one material": (single_slot, PANEL_SLOT_MATERIALS[:48]),
    }
    for label, (mutate, records) in accepted.items():
        variant = deepcopy(document)
        mutate(variant)
        geometry = cook_gltf_geometry.normalized_geometry(variant, buffer)
        lines = cook_gltf_geometry.subset_lines(geometry)
        if (geometry["slot_materials"] != records or "slot_material_stride=48" not in lines or
                "slot_materials_b64=" + base64.b64encode(records).decode("ascii") not in lines):
            print(f"glTF cooker self-test failed: cooked {label} wrong", file=sys.stderr)
            return 1

    def material(index: int, **fields):
        return lambda d: d["materials"][index].update(fields)

    def factors(index: int, **fields):
        return lambda d: d["materials"][index]["pbrMetallicRoughness"].update(fields)

    # The panel declares no textures, so every texture slot is read and checked.
    textures = "names a missing texture"
    unsupported = "unsupported material properties"
    rejected = {
        "a primitive without a material beside bound ones":
            (lambda d: d["meshes"][0]["primitives"][1].pop("material"), "every primitive must bind"),
        "a material index outside the document":
            (lambda d: d["meshes"][0]["primitives"][2].update(material=2), "every primitive must bind"),
        "17 materials": (lambda d: d["materials"].extend({"name": "extra"} for _ in range(15)), "at most 16"),
        "17 primitives": (lambda d: d["meshes"][0]["primitives"].extend(
            deepcopy(d["meshes"][0]["primitives"][1]) for _ in range(14)), "1 to 16 primitives"),
        "a line primitive": (lambda d: d["meshes"][0]["primitives"][1].update(mode=1), "triangle primitives only"),
        "shared positions with different attributes":
            (lambda d: d["meshes"][0]["primitives"][2]["attributes"].pop("NORMAL"), "share every vertex attribute"),
        "attributes that are not an accessor map":
            (lambda d: d["meshes"][0]["primitives"][1].update(attributes=[0]), "accessors by index"),
        "an attribute naming an accessor by list": (lambda d: d["meshes"][0]["primitives"][1].update(
            attributes={"POSITION": [3]}), "accessors by index"),
        "a base-color texture": (factors(0, baseColorTexture={"index": 0}), textures),
        "a metallic-roughness texture": (factors(1, metallicRoughnessTexture={"index": 0}), textures),
        "a normal map": (material(1, normalTexture={"index": 0}), textures),
        "an occlusion map": (material(0, occlusionTexture={"index": 0}), textures),
        "an emissive map": (material(0, emissiveTexture={"index": 0}), textures),
        "an alpha-masked material": (material(1, alphaMode="MASK"), "alpha-mask materials need a base-color texture"),
        "an unknown alpha mode": (material(1, alphaMode="ADD"), "alphaMode must be OPAQUE, MASK, or BLEND"),
        "a material extension": (material(0, extensions={"KHR_materials_unlit": {}}), unsupported),
        "material extras": (material(0, extras={}), unsupported),
        "an unknown PBR property": (factors(0, specularFactor=1.0), unsupported),
        "a material that is not an object": (lambda d: d["materials"].__setitem__(0, "center"), "must be an object"),
        "PBR factors that are not an object": (material(0, pbrMetallicRoughness=[]), "must be an object"),
        "a base color above 1": (factors(0, baseColorFactor=[1.5, 0.0, 0.0, 1.0]), "baseColorFactor must be a finite"),
        "a three-channel base color": (factors(0, baseColorFactor=[1.0, 0.0, 0.0]), "baseColorFactor must list 4"),
        "a negative roughness": (factors(1, roughnessFactor=-0.1), "roughnessFactor must be a finite"),
        "a non-finite metallic factor": (factors(1, metallicFactor=float("nan")), "metallicFactor must be a finite"),
        "a boolean metallic factor": (factors(1, metallicFactor=True), "metallicFactor must be a finite"),
        "a string emissive channel": (material(0, emissiveFactor=["1", 0.0, 0.0]), "emissiveFactor must be a finite"),
        "a two-channel emissive factor": (material(0, emissiveFactor=[1.0, 0.0]), "emissiveFactor must list 3"),
        "an alpha cutoff above 1": (material(0, alphaCutoff=2.0), "alphaCutoff must be a finite"),
        "a string doubleSided": (material(0, doubleSided="yes"), "doubleSided must be a boolean"),
    }
    for label, (mutate, reason) in rejected.items():
        variant = deepcopy(document)
        mutate(variant)
        try:
            cook_gltf_geometry.normalized_geometry(variant, buffer)
        except ValueError as error:
            if reason in str(error):
                continue
            print(f"glTF cooker self-test failed: rejected {label} for another reason: {error}", file=sys.stderr)
            return 1
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
        help="add a PNG, JPEG or bounded 2D KTX2 image as a named .elpk section")
    parser.add_argument("--dependency", action="append", default=[], metavar="BUNDLE",
        help="name a bundle this .elpk needs, relative to the output's directory")
    parser.add_argument("--simplify-ratio", type=float,
        help="cook a static LOD variant at this fraction of each placement/material triangle count")
    parser.add_argument("--lod-ratios",
        help="cook full detail plus descending ratios (for example 0.5,0.25); --output supplies a base name for <stem>.lod.json and content-addressed sibling packages")
    parser.add_argument("--self-test", action="store_true", help="cook the authored maze mesh twice")
    options = parser.parse_args(arguments)
    if options.self_test:
        if (options.source is not None or options.asset_path is not None or options.output is not None or
                options.simplify_ratio is not None or options.lod_ratios is not None):
            parser.error("--self-test cannot be combined with source, --asset-path, --output, or LOD options")
        return self_test()
    if options.source is None or options.asset_path is None or options.output is None:
        parser.error("source, --asset-path, and --output are required")
    try:
        textures = parse_texture_arguments(options.texture)
        if options.lod_ratios is not None and options.simplify_ratio is not None:
            raise ValueError("--lod-ratios and --simplify-ratio are mutually exclusive")
        if options.lod_ratios is not None:
            ratios = parse_lod_ratios(options.lod_ratios)
            manifest_path, levels = cook_lod_chain(options.source, options.asset_path,
                options.output, ratios, textures, options.dependency)
            print(f"cooked {len(levels)} LOD levels for {options.asset_path} -> {manifest_path}")
            for level in levels:
                print(f"level {level['index']}: {level['triangles']} triangles, "
                    f"{level['vertices']} vertices, {level['byte_size']} bytes, "
                    f"relative_error_budget={level['relative_error_budget']:.6f}, "
                    f"sha256={level['sha256']}")
            return 0
        if (textures or options.dependency) and options.output.suffix.lower() != ".elpk":
            raise ValueError("--texture and --dependency require an .elpk output bundle")
        if options.output.suffix.lower() == ".elpk":
            with tempfile.TemporaryDirectory(prefix="elisa-gltf-bundle-") as temporary:
                geometry_path, result = cook_gltf_geometry.cook_geometry_package(
                    options.source, options.asset_path, Path(temporary) / "geometry.pkg",
                    allow_textures=True, simplify_ratio=options.simplify_ratio)
                images = result["images"]
                if images.keys() & textures.keys():
                    raise ValueError("--texture names a section the source's material images use")
                images.update((name, path.read_bytes()) for name, path in textures.items())
                write_geometry_package(options.output, geometry_path.read_bytes(), images,
                    options.dependency)
            output = options.output.expanduser().resolve()
        else:
            output, result = cook_assets.cook_geometry_package(
                options.source, options.asset_path, options.output,
                simplify_ratio=options.simplify_ratio)
        reduction = (f"{result['triangles']}/{result['source_triangles']} triangles"
            if result["lod"] is not None else f"{result['triangles']} triangles")
        print(f"cooked {options.asset_path} -> {output} ({reduction}, {result['positions']} vertices)")
        if result["lod"] is not None:
            print(f"LOD ratio={result['lod']['ratio']:.3f}, "
                f"max relative error={result['lod']['maximum_error']:.5f}, "
                f"vertices={result['lod']['vertices']}/{result['lod']['source_vertices']}, "
                f"attribute bytes={result['lod']['attribute_bytes']}/"
                f"{result['lod']['source_attribute_bytes']}")
    except (OSError, RuntimeError, ValueError, KeyError, IndexError, TypeError, AttributeError) as failure:
        print(f"glTF cooking failed: {failure}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
