"""Write the ELPK bundles the native bundle-texture test registers.

`render-scene-textures.elpk` holds one valid PNG and one valid JPEG, plus
sections the runtime must reject or fail to decode. A second copy has one byte
of its PNG section flipped. A third copy lives outside the project root and is
reached through a symlink inside it.
"""

from __future__ import annotations

import shutil
import subprocess
import tempfile
from pathlib import Path

from elisa_package import write_package
from packaged_maze_smoke import section_span
from png_image import encode_png

BUNDLE = "render-scene-textures.elpk"
CORRUPTED_BUNDLE = "render-scene-textures-corrupt.elpk"
ESCAPE_LINK = "render-scene-textures-link.elpk"
# KTX2's file identifier. Wicked has no KTX decoder, so the runtime must refuse it.
KTX2_IDENTIFIER = b"\xabKTX 20\xbb\r\n\x1a\n"


def checker(width: int, height: int) -> bytes:
    pixels = bytearray()
    for row in range(height):
        for column in range(width):
            light = (row // 4 + column // 4) % 2 == 0
            pixels.extend((230, 200, 60, 255) if light else (40, 90, 200, 255))
    return bytes(pixels)


def jpeg_from_png(png: bytes) -> bytes:
    with tempfile.TemporaryDirectory(prefix="Elisa bundle texture ") as temporary:
        source = Path(temporary) / "source.png"
        output = Path(temporary) / "photo.jpg"
        source.write_bytes(png)
        subprocess.run(["/usr/bin/sips", "-s", "format", "jpeg", str(source), "--out", str(output)],
            check=True, stdout=subprocess.DEVNULL)
        return output.read_bytes()


def write_fixtures(cooked: Path, outside: Path) -> Path:
    """Write every fixture and return the escape link, which the caller removes."""
    albedo = encode_png(16, 8, checker(16, 8))
    # A well-formed IHDR that claims 65535 x 65535 pixels; no decoder may see it.
    huge = albedo[:16] + (65535).to_bytes(4, "big") * 2 + albedo[24:]
    sections = {
        "albedo": albedo,
        "photo": jpeg_from_png(encode_png(24, 12, checker(24, 12))),
        "huge": huge,
        # The signature and IHDR pass inspection; the image data is cut short.
        "truncated": albedo[:40],
        "ktx": KTX2_IDENTIFIER + bytes(68),
    }
    bundle = cooked / BUNDLE
    write_package(bundle, sections)

    original = bundle.read_bytes()
    corrupted = bytearray(original)
    offset, stored = section_span(original, "albedo")
    corrupted[offset + stored // 2] ^= 0xFF
    (cooked / CORRUPTED_BUNDLE).write_bytes(bytes(corrupted))

    escaped = outside / BUNDLE
    shutil.copyfile(bundle, escaped)
    link = cooked / ESCAPE_LINK
    link.unlink(missing_ok=True)
    link.symlink_to(escaped)
    return link
