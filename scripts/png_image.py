"""Encode small RGBA8 images as PNG files for cooked-asset fixtures."""

from __future__ import annotations

import struct
import zlib


def _chunk(kind: bytes, payload: bytes) -> bytes:
    return (struct.pack(">I", len(payload)) + kind + payload +
        struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF))


def encode_png(width: int, height: int, rgba: bytes) -> bytes:
    """Return a deterministic 8-bit RGBA PNG with no ancillary chunks."""
    if width <= 0 or height <= 0 or len(rgba) != width * height * 4:
        raise ValueError("PNG pixels must be width * height RGBA8 values")
    stride = width * 4
    # Filter type 0 (none) on every row keeps the output byte-stable.
    raw = b"".join(b"\x00" + rgba[row * stride:(row + 1) * stride] for row in range(height))
    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + _chunk(b"IHDR", header) +
        _chunk(b"IDAT", zlib.compress(raw, 9)) + _chunk(b"IEND", b""))
