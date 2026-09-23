"""Self-tests for FBX's bounded PNG surface-map decoder."""

from __future__ import annotations

import struct
import zlib

from fbx_surface_texture import ALPHA_BLEND, ALPHA_MASK, decode_png_rgba, infer_alpha_mode, pack_surface_maps
from png_image import encode_png


PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
CHANNELS_BY_COLOR_TYPE = {0: 1, 2: 3, 4: 2, 6: 4}


def _chunk(kind: bytes, payload: bytes) -> bytes:
    return (struct.pack(">I", len(payload)) + kind + payload +
        struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF))


def _filtered_png(color_type: int, width: int, rows: list[bytes]) -> tuple[bytes, bytes]:
    """Encode known samples with all five PNG filters and return their RGBA expectation."""
    channels = CHANNELS_BY_COLOR_TYPE[color_type]
    if any(len(row) != width * channels for row in rows):
        raise ValueError("invalid self-test PNG row")
    raw = bytearray()
    previous = bytes(width * channels)
    for row_index, row in enumerate(rows):
        filter_type = row_index
        filtered = bytearray()
        for index, value in enumerate(row):
            left = row[index - channels] if index >= channels else 0
            above = previous[index]
            upper_left = previous[index - channels] if index >= channels else 0
            if filter_type == 1:
                predictor = left
            elif filter_type == 2:
                predictor = above
            elif filter_type == 3:
                predictor = (left + above) // 2
            elif filter_type == 4:
                estimate = left + above - upper_left
                distances = (abs(estimate - left), abs(estimate - above), abs(estimate - upper_left))
                predictor = left if distances[0] <= min(distances[1:]) else (
                    above if distances[1] <= distances[2] else upper_left)
            else:
                predictor = 0
            filtered.append((value - predictor) & 0xFF)
        raw.append(filter_type)
        raw.extend(filtered)
        previous = row

    header = struct.pack(">IIBBBBB", width, len(rows), 8, color_type, 0, 0, 0)
    image = (PNG_SIGNATURE + _chunk(b"IHDR", header) +
        _chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + _chunk(b"IEND", b""))
    expected = bytearray()
    for row in rows:
        for pixel in range(width):
            source = pixel * channels
            if color_type == 0:
                gray = row[source]
                expected.extend((gray, gray, gray, 255))
            elif color_type == 2:
                expected.extend((*row[source:source + 3], 255))
            elif color_type == 4:
                gray = row[source]
                expected.extend((gray, gray, gray, row[source + 1]))
            else:
                expected.extend(row[source:source + 4])
    return image, bytes(expected)


def validate_surface_texture_decoder() -> None:
    """Check color-type conversion, PNG filters, surface channels, and rejection bounds."""
    for color_type, channels in CHANNELS_BY_COLOR_TYPE.items():
        rows = [bytes((row * 43 + index * 29 + 7) & 0xFF
            for index in range(2 * channels)) for row in range(5)]
        image, expected = _filtered_png(color_type, 2, rows)
        if decode_png_rgba(image) != (2, 5, expected):
            raise ValueError(f"PNG color type {color_type} or scanline filters decoded incorrectly")

    roughness = encode_png(2, 1, bytes((64, 1, 2, 255, 128, 3, 4, 255)))
    metalness = encode_png(2, 1, bytes((200, 5, 6, 255, 20, 7, 8, 255)))
    if decode_png_rgba(pack_surface_maps(roughness, metalness)) != (2, 1,
            bytes((255, 64, 200, 255, 255, 128, 20, 255))):
        raise ValueError("packed surface map channel placement is incorrect")
    if decode_png_rgba(pack_surface_maps(roughness, None)) != (2, 1,
            bytes((255, 64, 255, 255, 255, 128, 255, 255))):
        raise ValueError("roughness-only packing does not fill missing channels neutrally")
    if decode_png_rgba(pack_surface_maps(None, metalness)) != (2, 1,
            bytes((255, 255, 200, 255, 255, 255, 20, 255))):
        raise ValueError("metalness-only packing does not fill missing channels neutrally")
    cutout = encode_png(2, 1, bytes((220, 80, 40, 0, 40, 80, 220, 255)))
    translucent = encode_png(2, 1, bytes((220, 80, 40, 96, 40, 80, 220, 255)))
    opaque = encode_png(2, 1, bytes((220, 80, 40, 255, 40, 80, 220, 255)))
    rgb_png, _ = _filtered_png(2, 1, [bytes((220, 80, 40))])
    ihdr_end = len(PNG_SIGNATURE) + 4 + 4 + 13 + 4
    rgb_trns = (rgb_png[:ihdr_end] + _chunk(b"tRNS", struct.pack(">HHH", 220, 80, 40)) +
        rgb_png[ihdr_end:])
    if (infer_alpha_mode(cutout, 0) != ALPHA_MASK or
            infer_alpha_mode(translucent, 0) != ALPHA_BLEND or
            infer_alpha_mode(opaque, 0) != 0 or
            infer_alpha_mode(rgb_trns, 0) != ALPHA_BLEND or
            infer_alpha_mode(cutout, ALPHA_BLEND) != ALPHA_BLEND):
        raise ValueError("FBX PNG alpha samples did not infer the safe material alpha policy")

    damaged = bytearray(roughness)
    damaged[-1] ^= 1
    for invalid_pair in ((bytes(damaged), None), (roughness, b"not a PNG"),
            (roughness, encode_png(1, 1, bytes((1, 2, 3, 255))))):
        try:
            pack_surface_maps(*invalid_pair)
        except ValueError:
            continue
        raise ValueError("surface-map packing accepted a bad checksum, format, or dimension mismatch")
