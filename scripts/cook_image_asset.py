#!/usr/bin/env python3
"""Cook a PNG or JPEG texture into a bounded-size runtime copy.

Source art is often authored at 4096 or 8192 pixels, but the runtime decodes
every texture on the CPU before upload, so oversized images dominate scene
load time and resident memory. This tool resamples an image so neither side
exceeds `--max-size`, keeping its aspect ratio, and writes it beside the
cooked packages. It needs Pillow at cook time only; the runtime never does.
"""

from __future__ import annotations

import argparse
import io
from pathlib import Path
import sys
import tempfile

from png_image import encode_png

MAX_DIMENSION = 8192
IMAGE_SUFFIXES = (".png", ".jpg", ".jpeg")


class ImageCookError(Exception):
    pass


def _pillow():
    try:
        from PIL import Image
    except ImportError as error:
        raise ImageCookError("resampling textures needs Pillow (python3 -m pip install pillow)") from error
    return Image


def resample_image_bytes(payload: bytes, suffix: str, max_size: int) -> tuple[bytes, int, int]:
    """Return the image bounded to `max_size` on each side with its new dimensions."""
    if suffix.lower() not in IMAGE_SUFFIXES:
        raise ImageCookError("texture must be a .png, .jpg or .jpeg image")
    if isinstance(max_size, bool) or not isinstance(max_size, int) or not 1 <= max_size <= MAX_DIMENSION:
        raise ImageCookError(f"max size must be an integer from 1 to {MAX_DIMENSION}")
    image_module = _pillow()
    try:
        with image_module.open(io.BytesIO(payload)) as image:
            image.load()
            if image.format not in ("PNG", "JPEG"):
                raise ImageCookError("texture bytes are not a PNG or JPEG image")
            width, height = image.size
            if not 1 <= width <= MAX_DIMENSION or not 1 <= height <= MAX_DIMENSION:
                raise ImageCookError(f"texture dimensions must be within 1 to {MAX_DIMENSION}")
            if width <= max_size and height <= max_size:
                return payload, width, height
            scale = max_size / max(width, height)
            target = (max(1, round(width * scale)), max(1, round(height * scale)))
            mode = "RGBA" if image.mode in ("RGBA", "LA", "P") and suffix.lower() == ".png" else "RGB"
            resampled = image.convert(mode).resize(target, image_module.Resampling.LANCZOS)
            buffer = io.BytesIO()
            if suffix.lower() == ".png":
                resampled.save(buffer, format="PNG", compress_level=6)
            else:
                resampled.save(buffer, format="JPEG", quality=92)
            return buffer.getvalue(), target[0], target[1]
    except (OSError, ValueError) as error:
        raise ImageCookError(f"texture could not be decoded: {error}") from error


def cook_image(source: Path, output: Path, max_size: int) -> tuple[int, int]:
    source = source.expanduser().resolve()
    output = output.expanduser().resolve()
    if not source.is_file():
        raise ImageCookError(f"texture source does not exist: {source}")
    if output == source:
        raise ImageCookError("texture output cannot overwrite its source")
    if output.suffix.lower() != source.suffix.lower():
        raise ImageCookError("texture output must keep the source image type")
    payload, width, height = resample_image_bytes(source.read_bytes(), source.suffix, max_size)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(payload)
    return width, height


def self_test() -> int:
    try:
        _pillow()
    except ImageCookError as error:
        print(f"image cook self-test skipped: {error}")
        return 0
    with tempfile.TemporaryDirectory(prefix="elisa-image-cook-") as temporary:
        directory = Path(temporary)
        source = directory / "albedo.png"
        source.write_bytes(encode_png(8, 4, bytes([200, 40, 40, 255]) * 32))
        output = directory / "cooked" / "albedo.png"
        if cook_image(source, output, 4) != (4, 2) or not output.is_file():
            print("image cook self-test failed: the image was not bounded to 4 pixels", file=sys.stderr)
            return 1
        if cook_image(source, directory / "same.png", 8) != (8, 4) or \
                (directory / "same.png").read_bytes() != source.read_bytes():
            print("image cook self-test failed: a small image was rewritten", file=sys.stderr)
            return 1
        rejected = [
            ("wrong suffix", (source, directory / "albedo.jpg", 4)),
            ("zero size", (source, directory / "zero.png", 0)),
            ("missing source", (directory / "missing.png", directory / "out.png", 4)),
            ("overwriting source", (source, source, 4)),
        ]
        for label, arguments in rejected:
            try:
                cook_image(*arguments)
            except ImageCookError:
                continue
            print(f"image cook self-test failed: {label} was accepted", file=sys.stderr)
            return 1
    print("image cook self-test passed")
    return 0


def main(arguments: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", nargs="?", type=Path)
    parser.add_argument("--output", type=Path, help="destination image with the source's suffix")
    parser.add_argument("--max-size", type=int, help=f"largest side in pixels (1 to {MAX_DIMENSION})")
    parser.add_argument("--self-test", action="store_true", help="resample a small fixture and check it")
    options = parser.parse_args(arguments)
    if options.self_test:
        return self_test()
    if options.source is None or options.output is None or options.max_size is None:
        parser.error("source, --output and --max-size are required unless --self-test is used")
    try:
        width, height = cook_image(options.source, options.output, options.max_size)
    except ImageCookError as error:
        print(f"image cooking failed: {error}", file=sys.stderr)
        return 1
    print(f"Cooked texture: {options.output} ({width}x{height})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
