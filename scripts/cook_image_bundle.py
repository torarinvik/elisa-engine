#!/usr/bin/env python3
"""Cook PNG and JPEG images into an ELPK bundle of image sections."""

from __future__ import annotations

import argparse
from pathlib import Path
import sys
import tempfile

from elisa_package import parse_texture_arguments, write_image_bundle
from png_image import encode_png


def self_test() -> int:
    with tempfile.TemporaryDirectory(prefix="elisa-image-bundle-") as temporary:
        directory = Path(temporary)
        image = directory / "albedo.png"
        image.write_bytes(encode_png(2, 1, bytes([255, 0, 0, 255, 0, 0, 255, 255])))
        bundles = []
        for label in ("first", "second"):
            bundle = directory / f"{label}.elpk"
            if main(["--output", str(bundle), "--texture", f"albedo={image}",
                    "--dependency", "shared/detail.elpk"]) != 0:
                print("image bundle self-test failed: a PNG section was rejected", file=sys.stderr)
                return 1
            bundles.append(bundle.read_bytes())
        if bundles[0] != bundles[1] or b"albedo" not in bundles[0] or b"mesh" in bundles[0] or \
                b"dependency=shared/detail.elpk\n" not in bundles[0]:
            print("image bundle self-test failed: bundles differ or lack the section or manifest",
                file=sys.stderr)
            return 1
        rejected = [
            ("no image section", ["--output", str(directory / "empty.elpk")]),
            ("reserved mesh section", ["--output", str(directory / "mesh.elpk"), "--texture", f"mesh={image}"]),
            ("non-bundle output", ["--output", str(directory / "image.pkg"), "--texture", f"albedo={image}"]),
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
        help="add a PNG or JPEG image as a named section")
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
