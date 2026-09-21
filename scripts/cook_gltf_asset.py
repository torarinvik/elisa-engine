#!/usr/bin/env python3
"""Cook one bounded, static glTF triangle mesh into Elisa's runtime package."""

from __future__ import annotations

import argparse
from copy import deepcopy
from pathlib import Path
import sys
import tempfile

import cook_assets
import cook_gltf_geometry


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
        rejected.append(("source material binding", material_bound))
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
    print("glTF cooker self-test passed: deterministic 12-triangle runtime package")
    return 0


def main(arguments: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", nargs="?", type=Path, help="source glTF file")
    parser.add_argument("--asset-path", help="safe project-relative source identity")
    parser.add_argument("--output", type=Path, help="destination runtime package")
    parser.add_argument("--self-test", action="store_true", help="cook the authored maze mesh twice")
    options = parser.parse_args(arguments)
    if options.self_test:
        if options.source is not None or options.asset_path is not None or options.output is not None:
            parser.error("--self-test cannot be combined with source, --asset-path, or --output")
        return self_test()
    if options.source is None or options.asset_path is None or options.output is None:
        parser.error("source, --asset-path, and --output are required")
    try:
        output, result = cook_assets.cook_geometry_package(
            options.source, options.asset_path, options.output)
        print(f"cooked {options.asset_path} -> {output} "
            f"({result['triangles']} triangles, {result['positions']} vertices)")
    except (OSError, ValueError, KeyError, IndexError, TypeError, AttributeError) as failure:
        print(f"glTF cooking failed: {failure}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
