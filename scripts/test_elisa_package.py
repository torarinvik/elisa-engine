#!/usr/bin/env python3
"""Verify Python ELPK output with the production C++ package reader."""

from __future__ import annotations

import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

from elisa_package import MAX_SECTIONS, build_package_bytes, write_package


ROOT = Path(__file__).resolve().parents[1]


def rejects(callable_value) -> bool:
    try:
        callable_value()
    except (OSError, RuntimeError, TypeError, UnicodeError, ValueError):
        return True
    return False


def cooked_triangle_package() -> bytes:
    def encoded(format_string: str, values: tuple[float, ...] | tuple[int, ...]) -> str:
        import base64
        return base64.b64encode(struct.pack(format_string, *values)).decode("ascii")

    sections = [
        "format=elisa-cooked-v2", "source=test/triangle.gltf", "source_sha256=" + "0" * 64,
        "triangles=1", "positions=3", "indices=3", "position_stride=12",
        "normal_stride=12", "uv_stride=8", "index_stride=4",
        "positions_b64=" + encoded("<9f", (0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0)),
        "normals_b64=" + encoded("<9f", (0.0, 0.0, 1.0) * 3),
        "uvs_b64=" + encoded("<6f", (0.0, 0.0, 1.0, 0.0, 0.0, 1.0)),
        "indices_b64=" + encoded("<3I", (0, 1, 2)),
    ]
    return ("\n".join(sections) + "\n").encode("ascii")


def main(arguments: list[str]) -> int:
    if len(arguments) > 1:
        print("usage: test_elisa_package.py [ELPK_ASSET]", file=sys.stderr)
        return 2
    compiler = os.environ.get("CXX", "c++")
    if shutil.which(compiler) is None:
        print(f"C++ compiler is unavailable: {compiler}", file=sys.stderr)
        return 2
    sections = {"mesh": cooked_triangle_package(), "texture": bytes(range(256))}
    dependencies = ("foundation.elpk", "base.elpk")
    first = build_package_bytes(sections, dependencies)
    second = build_package_bytes(sections, dependencies)
    if first != second:
        print("ELPK output is not deterministic", file=sys.stderr)
        return 1
    if not rejects(lambda: build_package_bytes({"../mesh": b"data"})) or \
            not rejects(lambda: build_package_bytes({"mesh": b""})) or \
            not rejects(lambda: build_package_bytes({"mesh": b"data"}, ("../base.elpk",))):
        print("ELPK writer accepted an invalid section or dependency", file=sys.stderr)
        return 1
    wide_sections = {f"section_{index:04d}": bytes([index & 0xFF])
        for index in range(MAX_SECTIONS - 1)}
    if len(build_package_bytes(wide_sections)) == 0 or not rejects(lambda:
            build_package_bytes({**wide_sections, "section_overflow": b"x"})):
        print("ELPK writer accepted an invalid section count", file=sys.stderr)
        return 1

    def legacy_package(section_count: int) -> bytes:
        return b"".join(f"field_{index:03d}=x\n".encode("ascii")
            for index in range(section_count))

    with tempfile.TemporaryDirectory(prefix="elisa-elpk-test-") as temporary:
        package = Path(temporary) / "assets.elpk"
        write_package(package, sections, dependencies)
        wide_package = Path(temporary) / "wide.elpk"
        write_package(wide_package, wide_sections)
        legacy_package_path = Path(temporary) / "many-sections.pkg"
        legacy_package_path.write_bytes(legacy_package(135))
        excess_legacy_package_path = Path(temporary) / "too-many-sections.pkg"
        excess_legacy_package_path.write_bytes(legacy_package(MAX_SECTIONS + 1))
        executable = Path(temporary) / "package-format-test"
        command = [compiler, "-std=c++17", "-O2", "-I", str(ROOT / "native"),
            "-I", "/opt/homebrew/include", "-L", os.environ.get("ZSTD_LIBRARY_DIR", "/opt/homebrew/lib"),
            str(ROOT / "native/package_format_test.cpp"),
            "-lzstd", "-o", str(executable)]
        built = subprocess.run(command, capture_output=True, text=True, check=False)
        if built.returncode != 0:
            print(built.stderr or built.stdout, file=sys.stderr)
            return built.returncode
        checked = subprocess.run([str(executable), str(package), str(wide_package),
            str(legacy_package_path), str(excess_legacy_package_path)], capture_output=True,
            text=True, check=False)
        if checked.returncode != 0:
            print(checked.stderr or checked.stdout, file=sys.stderr)
            return checked.returncode
        print(checked.stdout.strip())
        if arguments:
            asset = Path(arguments[0]).expanduser().resolve(strict=True)
            asset_check = subprocess.run([str(executable), str(asset)], capture_output=True,
                text=True, check=False)
            if asset_check.returncode != 0:
                print(asset_check.stderr or asset_check.stdout, file=sys.stderr)
                return asset_check.returncode
            print(asset_check.stdout.strip())
    print("ELPK writer deterministic, bounded, and native-reader compatible.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
