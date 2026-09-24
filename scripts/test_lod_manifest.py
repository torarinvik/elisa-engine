#!/usr/bin/env python3
"""Sanitized native LOD-manifest and referenced-package validation."""

from __future__ import annotations

import copy
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

import cook_gltf_lod
from cook_gltf_lod_package import cook_lod_chain, parse_lod_ratios


ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    compiler = os.environ.get("CXX", "c++")
    if shutil.which(compiler) is None:
        print(f"C++ compiler is unavailable: {compiler}", file=sys.stderr)
        return 2
    with tempfile.TemporaryDirectory(prefix="elisa-lod-manifest-") as temporary_name:
        directory = Path(temporary_name)
        source = cook_gltf_lod.write_test_source(directory)
        manifest_path, levels = cook_lod_chain(source, "test/lod-grid.gltf",
            directory / "grid.pkg", parse_lod_ratios("0.5,0.25"))
        unused_source = cook_gltf_lod.write_test_source_with_unused_vertices(directory)
        _, unused_levels = cook_lod_chain(unused_source, "test/lod-grid-unused.gltf",
            directory / "unused.pkg", parse_lod_ratios("0.5"))
        source_positions = cook_gltf_lod.test_fixture()["vertex_count"] * 2
        if (len(unused_levels) != 2 or unused_levels[0]["triangles"] <= 0 or
                unused_levels[0]["vertices"] <= 0 or unused_levels[0]["vertices"] >= source_positions):
            print("LOD cooker did not compact unreferenced source vertices", file=sys.stderr)
            return 1
        original = json.loads(manifest_path.read_text(encoding="utf-8"))
        rejected_paths = []

        def rejected(name: str, mutate) -> None:
            document = copy.deepcopy(original)
            mutate(document)
            path = directory / name
            path.write_text(json.dumps(document, sort_keys=True, separators=(",", ":")) + "\n",
                encoding="utf-8")
            rejected_paths.append(path)

        rejected("traversal.json", lambda document: document["levels"][1].__setitem__(
            "package", "../escape.pkg"))
        rejected("count.json", lambda document: document.__setitem__("level_count", 2))
        rejected("order.json", lambda document: document["levels"][2].__setitem__(
            "simplify_ratio", 0.75))

        def duplicate_hash(document: dict) -> None:
            second = document["levels"][1]
            old_digest = second["sha256"]
            new_digest = document["levels"][0]["sha256"]
            second["sha256"] = new_digest
            second["package"] = second["package"].replace(old_digest[:16], new_digest[:16])

        rejected("duplicate-hash.json", duplicate_hash)
        rejected("name-hash.json", lambda document: document["levels"][1].__setitem__(
            "package", document["levels"][1]["package"].replace(
                document["levels"][1]["sha256"][:16], "0" * 16)))

        tampered_document = copy.deepcopy(original)
        tampered_level = tampered_document["levels"][1]
        tampered_name = f"tampered.lod-01-{tampered_level['sha256'][:16]}{Path(tampered_level['package']).suffix}"
        original_package = directory / tampered_level["package"]
        tampered_package = directory / tampered_name
        shutil.copyfile(original_package, tampered_package)
        damaged = bytearray(tampered_package.read_bytes())
        damaged[-1] ^= 1
        tampered_package.write_bytes(damaged)
        tampered_level["package"] = tampered_name
        tampered_manifest = directory / "tampered-package.json"
        tampered_manifest.write_text(json.dumps(tampered_document, sort_keys=True,
            separators=(",", ":")) + "\n", encoding="utf-8")

        outside_package = directory.parent / f"{directory.name}-outside.pkg"
        outside_package.write_bytes(original_package.read_bytes())
        escaped_document = copy.deepcopy(original)
        escaped_level = escaped_document["levels"][1]
        escaped_name = f"escape.lod-01-{escaped_level['sha256'][:16]}{Path(escaped_level['package']).suffix}"
        (directory / escaped_name).symlink_to(outside_package)
        escaped_level["package"] = escaped_name
        escaped_manifest = directory / "escaped-package.json"
        escaped_manifest.write_text(json.dumps(escaped_document, sort_keys=True,
            separators=(",", ":")) + "\n", encoding="utf-8")

        executable = directory / "lod-manifest-test"
        command = [compiler, "-std=c++17", "-O1", "-g", "-fno-omit-frame-pointer",
            "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
            "-I", str(ROOT / "native"), "-I", str(ROOT / "dependencies/meshoptimizer"),
            "-I", "/opt/homebrew/include",
            "-L", os.environ.get("ZSTD_LIBRARY_DIR", "/opt/homebrew/lib"),
            str(ROOT / "native/lod_manifest_test.cpp"),
            str(ROOT / "native/meshopt_stream_codec.cpp"),
            str(ROOT / "dependencies/meshoptimizer/indexcodec.cpp"),
            str(ROOT / "dependencies/meshoptimizer/vertexcodec.cpp"),
            "-lzstd", "-o", str(executable)]
        built = subprocess.run(command, capture_output=True, text=True, check=False)
        if built.returncode != 0:
            print(built.stderr or built.stdout, file=sys.stderr)
            return built.returncode
        checked = subprocess.run([str(executable), str(manifest_path), "--reject-chain",
            str(tampered_manifest), "--reject-chain", str(escaped_manifest),
            *(str(path) for path in rejected_paths)],
            capture_output=True, text=True, check=False)
        outside_package.unlink(missing_ok=True)
        sys.stdout.write(checked.stdout)
        sys.stderr.write(checked.stderr)
        if checked.returncode != 0 or f"{len(levels)} levels hash-verified" not in checked.stdout:
            print(f"LOD manifest validation failed with exit {checked.returncode}", file=sys.stderr)
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
