#!/usr/bin/env python3
"""Cook PNG, JPEG and bounded 2D KTX2 images into an ELPK bundle."""

from __future__ import annotations

import argparse
from pathlib import Path
import struct
import sys
import tempfile

from elisa_package import KTX2_IDENTIFIER, parse_texture_arguments, write_image_bundle
from png_image import encode_png


def self_test() -> int:
    with tempfile.TemporaryDirectory(prefix="elisa-image-bundle-") as temporary:
        directory = Path(temporary)
        image = directory / "albedo.png"
        image.write_bytes(encode_png(2, 1, bytes([255, 0, 0, 255, 0, 0, 255, 255])))
        ktx2 = directory / "normal.ktx2"
        ktx2_bytes = bytearray(108)
        ktx2_bytes[:12] = KTX2_IDENTIFIER
        struct.pack_into("<6I", ktx2_bytes, 20, 4, 4, 0, 0, 1, 1)
        struct.pack_into("<QQQ", ktx2_bytes, 80, 104, 4, 64)
        ktx2_bytes[104:] = b"KTX!"
        ktx2.write_bytes(ktx2_bytes)
        truncated_ktx2 = directory / "truncated.ktx2"
        truncated_ktx2.write_bytes(ktx2_bytes[:-1])
        oversized_ktx2 = directory / "oversized.ktx2"
        invalid_dimensions = bytearray(ktx2_bytes)
        struct.pack_into("<I", invalid_dimensions, 20, 8193)
        oversized_ktx2.write_bytes(invalid_dimensions)
        invalid_basis_headers = []
        for label, offset, value in (("pixel-format", 12, 37), ("supercompression", 44, 3)):
            invalid_header = bytearray(ktx2_bytes)
            struct.pack_into("<I", invalid_header, offset, value)
            path = directory / f"{label}.ktx2"
            path.write_bytes(invalid_header)
            invalid_basis_headers.append((label, path))
        invalid_shapes = []
        for label, offset, value in (("array", 32, 2), ("cubemap", 36, 6),
                ("volume", 28, 1), ("mips", 40, 17)):
            invalid_shape = bytearray(ktx2_bytes)
            struct.pack_into("<I", invalid_shape, offset, value)
            path = directory / f"{label}.ktx2"
            path.write_bytes(invalid_shape)
            invalid_shapes.append((label, path))
        bundles = []
        for label in ("first", "second"):
            bundle = directory / f"{label}.elpk"
            if main(["--output", str(bundle), "--texture", f"albedo={image}",
                    "--texture", f"normal={ktx2}",
                    "--dependency", "shared/detail.elpk"]) != 0:
                print("image bundle self-test failed: a PNG or KTX2 section was rejected", file=sys.stderr)
                return 1
            bundles.append(bundle.read_bytes())
        if bundles[0] != bundles[1] or b"albedo" not in bundles[0] or b"normal" not in bundles[0] or \
                b"mesh" in bundles[0] or \
                b"dependency=shared/detail.elpk\n" not in bundles[0]:
            print("image bundle self-test failed: bundles differ or lack the section or manifest",
                file=sys.stderr)
            return 1
        rejected = [
            ("no image section", ["--output", str(directory / "empty.elpk")]),
            ("reserved mesh section", ["--output", str(directory / "mesh.elpk"), "--texture", f"mesh={image}"]),
            ("non-bundle output", ["--output", str(directory / "image.pkg"), "--texture", f"albedo={image}"]),
            ("truncated KTX2 level payload", ["--output", str(directory / "truncated.elpk"),
                "--texture", f"basis={truncated_ktx2}"]),
            ("oversized KTX2 dimensions", ["--output", str(directory / "oversized.elpk"),
                "--texture", f"basis={oversized_ktx2}"]),
            *((f"KTX2 {label}", ["--output", str(directory / f"{label}.elpk"),
                "--texture", f"basis={path}"]) for label, path in invalid_basis_headers),
            *((f"KTX2 {label} shape", ["--output", str(directory / f"{label}.elpk"),
                "--texture", f"basis={path}"]) for label, path in invalid_shapes),
            ("escaping dependency", ["--output", str(directory / "escape.elpk"),
                "--texture", f"albedo={image}", "--dependency", "../shared.elpk"]),
            ("missing image", ["--output", str(directory / "missing.elpk"),
                "--texture", f"albedo={directory / 'missing.png'}"]),
        ]
        for label, arguments in rejected:
            try:
                status = main(arguments)
            except SystemExit as exit_request:
                status = exit_request.code
            if status == 0:
                print(f"image bundle self-test failed: accepted {label}", file=sys.stderr)
                return 1
    print("image bundle self-test passed: deterministic image sections and sorted dependencies")
    return 0


def main(arguments: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, help="destination .elpk bundle")
    parser.add_argument("--texture", action="append", default=[], metavar="SECTION=PATH",
        help="add a PNG, JPEG or bounded 2D KTX2 image as a named section")
    parser.add_argument("--dependency", action="append", default=[], metavar="BUNDLE",
        help="name a bundle this one needs, relative to the output's directory")
    parser.add_argument("--self-test", action="store_true", help="cook and check a small fixture bundle")
    options = parser.parse_args(arguments)
    if options.self_test:
        if options.output is not None or options.texture or options.dependency:
            parser.error("--self-test cannot be combined with other options")
        return self_test()
    if options.output is None or options.output.suffix.lower() != ".elpk":
        parser.error("--output must name an .elpk bundle")
    try:
        textures = parse_texture_arguments(options.texture)
        write_image_bundle(options.output,
            {name: path.read_bytes() for name, path in textures.items()}, options.dependency)
        print(f"cooked {len(textures)} image sections -> {options.output.expanduser().resolve()}")
    except (OSError, ValueError) as failure:
        print(f"image bundle cooking failed: {failure}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
