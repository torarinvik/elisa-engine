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
import struct
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
MAX_PACKAGE_BYTES = 64 * 1024 * 1024
MAX_LINE_BYTES = 16 * 1024 * 1024
MAX_FILE_BYTES = 512 * 1024 * 1024


def run(command: list[str]) -> None:
    print("+", " ".join(repr(argument) for argument in command), flush=True)
    subprocess.run(command, cwd=ROOT, check=True)


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
        "positions_b64", "normals_b64", "uvs_b64", "indices_b64",
    }
    if not required.issubset(fields):
        raise ValueError("cooked package is missing normalized geometry sections")
    if fields["format"] != "elisa-cooked-v2" or fields["source"] != expected_source:
        raise ValueError("cooked package format or source identity does not match")
    if fields["source_sha256"] != expected_hash:
        raise ValueError("cooked package source hash does not match the input FBX")
    if (fields["position_stride"], fields["normal_stride"], fields["uv_stride"], fields["index_stride"]) != (
            "12", "12", "8", "4"):
        raise ValueError("cooked package has unsupported geometry strides")

    def decode(name: str) -> bytes:
        try:
            return base64.b64decode(fields[name], validate=True)
        except (ValueError, binascii.Error) as failure:
            raise ValueError(f"cooked package has invalid base64 in {name}") from failure

    positions = decode("positions_b64")
    normals = decode("normals_b64")
    uvs = decode("uvs_b64")
    indices = decode("indices_b64")
    try:
        triangles = int(fields["triangles"])
        vertex_count = int(fields["positions"])
        index_count = int(fields["indices"])
    except ValueError as failure:
        raise ValueError("cooked package has invalid geometry counts") from failure
    if (triangles <= 0 or vertex_count <= 0 or index_count != triangles * 3 or
            len(positions) != vertex_count * 12 or len(normals) != vertex_count * 12 or
            len(uvs) != vertex_count * 8 or len(indices) != index_count * 4):
        raise ValueError("cooked package geometry counts and byte lengths disagree")
    if not all(math.isfinite(value) for (value,) in struct.iter_unpack("<f", positions + normals + uvs)):
        raise ValueError("cooked package contains non-finite geometry")
    if any(value >= vertex_count for (value,) in struct.iter_unpack("<I", indices)):
        raise ValueError("cooked package contains an out-of-range index")
    return fields


def build_cooker(build_dir: Path) -> Path:
    dependency = ROOT / "dependencies/ufbx"
    source = dependency / "ufbx.c"
    header = dependency / "ufbx.h"
    if not source.is_file() or not header.is_file():
        raise ValueError("missing pinned ufbx files; run python3 scripts/fetch_dependencies.py")
    cc = os.environ.get("CC", "cc")
    cxx = os.environ.get("CXX", "c++")
    object_file = build_dir / "ufbx.o"
    executable = build_dir / "fbx-asset-cooker"
    run([cc, "-std=c99", "-O2", "-I", str(dependency), "-c", str(source), "-o", str(object_file)])
    run([cxx, "-std=c++17", "-O2", "-I", str(dependency), "-I", str(ROOT / "native"),
        str(ROOT / "native/fbx_asset_cooker.cpp"), str(object_file), "-o", str(executable)])
    return executable


def cook_one(cooker: Path, source: Path, asset_path: str, output: Path) -> dict[str, str]:
    source = source.expanduser().resolve(strict=True)
    if not source.is_file():
        raise ValueError("FBX source must be a regular file")
    if source.stat().st_size == 0 or source.stat().st_size > MAX_FILE_BYTES:
        raise ValueError("FBX source is empty or exceeds the 512 MiB source limit")
    key = validate_asset_key(asset_path)
    digest = source_hash(source)
    output = output.expanduser().resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    run([str(cooker), "--source", str(source), "--asset-path", key,
        "--output", str(output), "--sha256", digest])
    return parse_package(output, key, digest)


def main(arguments: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", nargs="?", type=Path, help="source FBX file")
    parser.add_argument("--asset-path", help="project-relative identity recorded in the cooked package")
    parser.add_argument("--output", type=Path, help="destination .pkg path")
    parser.add_argument("--self-test", action="store_true", help="cook and validate the synthetic triangle fixture")
    options = parser.parse_args(arguments)
    if not options.self_test and (options.source is None or options.asset_path is None or options.output is None):
        parser.error("source, --asset-path, and --output are required unless --self-test is used")
    if options.self_test and (options.source is not None or options.asset_path is not None or options.output is not None):
        parser.error("--self-test cannot be combined with source, --asset-path, or --output")

    try:
        with tempfile.TemporaryDirectory(prefix="elisa-fbx-cooker-") as temporary:
            directory = Path(temporary)
            cooker = build_cooker(directory)
            if options.self_test:
                source = ROOT / "test/fixtures/fbx_triangle.fbx"
                output = directory / "triangle.pkg"
                fields = cook_one(cooker, source, "test/fixtures/fbx_triangle.fbx", output)
                if int(fields["triangles"]) != 1 or int(fields["positions"]) != 3:
                    raise ValueError("triangle fixture package counts do not match")
                print("FBX cooker self-test passed: one normalized triangle package")
            else:
                fields = cook_one(cooker, options.source, options.asset_path, options.output)
                print(f"FBX package validated: {fields['triangles']} triangles, {fields['positions']} vertices")
    except (OSError, ValueError, subprocess.CalledProcessError) as failure:
        print(f"FBX cooking failed: {failure}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
