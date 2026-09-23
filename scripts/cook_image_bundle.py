#!/usr/bin/env python3
"""Cook PNG, JPEG and bounded 2D KTX2 images into an ELPK bundle."""

from __future__ import annotations

import argparse
from pathlib import Path
import struct
import sys
import tempfile

from elisa_package import encoded_image_dimensions, parse_texture_arguments, write_image_bundle
from ktx2_fixtures import make_ktx2
from png_image import encode_png


def self_test() -> int:
    with tempfile.TemporaryDirectory(prefix="elisa-image-bundle-") as temporary:
        directory = Path(temporary)
        image = directory / "albedo.png"
        image.write_bytes(encode_png(2, 1, bytes([255, 0, 0, 255, 0, 0, 255, 255])))
        ktx2 = directory / "normal.ktx2"
        ktx2_bytes = make_ktx2()
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
        for label, offset, value in (("array", 32, 2), ("single-layer-array", 32, 1),
                ("cubemap", 36, 6),
                ("volume", 28, 1), ("mips", 40, 17)):
            invalid_shape = bytearray(ktx2_bytes)
            struct.pack_into("<I", invalid_shape, offset, value)
            path = directory / f"{label}.ktx2"
            path.write_bytes(invalid_shape)
            invalid_shapes.append((label, path))
        if any(encoded_image_dimensions(fixture) != (4, 4) for fixture in (
                make_ktx2(key_values=(("testkey", b"rd"),)),
                make_ktx2(scheme=1, key_values=(("testkey", b"rd"),),
                    global_data=b"basis-global"),
                make_ktx2(levels=(b"BASE", b"MIP!")))):
            print("image bundle self-test failed: a valid KTX2 metadata or mip layout was rejected",
                file=sys.stderr)
            return 1

        def invalid_structure(label: str, fixture: bytes, offset: int,
                value: int, width: int = 4) -> tuple[str, Path]:
            malformed = bytearray(fixture)
            format_code = {2: "<H", 4: "<I", 8: "<Q"}[width]
            struct.pack_into(format_code, malformed, offset, value)
            path = directory / f"structure-{label}.ktx2"
            path.write_bytes(malformed)
            return label, path

        metadata_fixture = make_ktx2(key_values=(("testkey", b"rd"),))
        basis_lz_fixture = make_ktx2(scheme=1, key_values=(("testkey", b"rd"),),
            global_data=b"basis-global")
        two_level_fixture = make_ktx2(levels=(b"BASE", b"MIP!"))
        invalid_structures = [
            invalid_structure("dfd-before-index", ktx2_bytes, 48, 100),
            invalid_structure("dfd-overflow", ktx2_bytes, 48, 0xFFFFFFFF),
            invalid_structure("dfd-empty", ktx2_bytes, 52, 0),
            invalid_structure("dfd-total-size", ktx2_bytes, 104, 43),
            invalid_structure("dfd-block-size", ktx2_bytes, 114, 39, 2),
            invalid_structure("type-size", ktx2_bytes, 16, 2),
            invalid_structure("kvd-offset-without-length", ktx2_bytes, 56, 148),
            invalid_structure("kvd-out-of-range", metadata_fixture, 60, 0xFFFFFFFF),
            invalid_structure("kvd-entry-overflow", metadata_fixture, 148, 0xFFFFFFFF),
            invalid_structure("sgd-length-without-offset", ktx2_bytes, 72, 8, 8),
            invalid_structure("sgd-out-of-range", basis_lz_fixture, 64, 0xFFFFFFFF, 8),
            invalid_structure("sgd-misaligned", basis_lz_fixture, 64, 169, 8),
            invalid_structure("level-overlaps-dfd", ktx2_bytes, 80, 104, 8),
            invalid_structure("level-index-overflow", ktx2_bytes, 80, 0xFFFFFFFFFFFFFFFF, 8),
            invalid_structure("level-payload-overflow", ktx2_bytes, 88, 0xFFFFFFFFFFFFFFFF, 8),
            invalid_structure("uncompressed-size", ktx2_bytes, 96, 3, 8),
            invalid_structure("overlapping-mips", two_level_fixture, 104, 172, 8),
        ]
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
            *((f"KTX2 structure {label}", ["--output", str(directory / f"structure-{label}.elpk"),
                "--texture", f"basis={path}"]) for label, path in invalid_structures),
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
