"""Bounded PNG decoding and deterministic FBX surface-map packing."""

from __future__ import annotations

import struct
import zlib

from elisa_package import MAX_IMAGE_DIMENSION
from png_image import encode_png


PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
PNG_CHUNK_LENGTH_BYTES = 4
PNG_CHUNK_TYPE_BYTES = 4
PNG_CHUNK_CRC_BYTES = 4
PNG_HEADER_BYTES = 13
PNG_CHUNK_OVERHEAD_BYTES = (PNG_CHUNK_LENGTH_BYTES + PNG_CHUNK_TYPE_BYTES + PNG_CHUNK_CRC_BYTES)
PNG_COLOR_TYPE_GRAY = 0
PNG_COLOR_TYPE_RGB = 2
PNG_COLOR_TYPE_GRAY_ALPHA = 4
PNG_COLOR_TYPE_RGBA = 6
PNG_SUPPORTED_COLOR_TYPES = (PNG_COLOR_TYPE_GRAY, PNG_COLOR_TYPE_RGB,
    PNG_COLOR_TYPE_GRAY_ALPHA, PNG_COLOR_TYPE_RGBA)
PNG_CHANNELS_BY_COLOR_TYPE = {
    PNG_COLOR_TYPE_GRAY: 1,
    PNG_COLOR_TYPE_RGB: 3,
    PNG_COLOR_TYPE_GRAY_ALPHA: 2,
    PNG_COLOR_TYPE_RGBA: 4,
}
PNG_FILTER_NONE = 0
PNG_FILTER_SUB = 1
PNG_FILTER_UP = 2
PNG_FILTER_AVERAGE = 3
PNG_FILTER_PAETH = 4
PNG_MAX_FILTER = PNG_FILTER_PAETH
PNG_BIT_DEPTH_8 = 8
PNG_INTERLACE_NONE = 0
RGBA_CHANNEL_COUNT = 4
RGBA_RED_CHANNEL = 0
RGBA_GREEN_CHANNEL = 1
RGBA_BLUE_CHANNEL = 2
RGBA_ALPHA_CHANNEL = 3
OPAQUE_ALPHA = 255
NEUTRAL_SURFACE_CHANNEL = 255
MAX_DECODED_RGBA_BYTES = 64 * 1024 * 1024


def decode_png_rgba(data: bytes) -> tuple[int, int, bytes]:
    """Decode a bounded, non-interlaced 8-bit PNG to tightly packed RGBA8."""
    signature_length = len(PNG_SIGNATURE)
    if len(data) < signature_length or data[:signature_length] != PNG_SIGNATURE:
        raise ValueError("separate FBX roughness and metalness maps must be PNG images")

    width = height = color_type = 0
    compressed = bytearray()
    offset = signature_length
    saw_header = saw_data = saw_end = data_ended = False
    while offset < len(data):
        if len(data) - offset < PNG_CHUNK_OVERHEAD_BYTES:
            raise ValueError("PNG chunk is truncated")
        length = struct.unpack_from(">I", data, offset)[0]
        type_offset = offset + PNG_CHUNK_LENGTH_BYTES
        payload_offset = type_offset + PNG_CHUNK_TYPE_BYTES
        kind = data[type_offset:payload_offset]
        end = offset + PNG_CHUNK_OVERHEAD_BYTES + length
        if length > len(data) - offset - PNG_CHUNK_OVERHEAD_BYTES or end > len(data):
            raise ValueError("PNG chunk length exceeds the image")
        payload = data[payload_offset:payload_offset + length]
        crc_offset = payload_offset + length
        expected_crc = struct.unpack_from(">I", data, crc_offset)[0]
        if zlib.crc32(kind + payload) & 0xFFFFFFFF != expected_crc:
            raise ValueError("PNG chunk checksum is invalid")
        if not all(65 <= byte <= 90 or 97 <= byte <= 122 for byte in kind):
            raise ValueError("PNG chunk type is invalid")
        if kind[2] & 0x20:
            raise ValueError("PNG chunk uses the reserved lowercase type bit")

        if not saw_header:
            if kind != b"IHDR" or length != PNG_HEADER_BYTES:
                raise ValueError("PNG must begin with one valid IHDR chunk")
            width, height, bit_depth, color_type, compression, filtering, interlace = struct.unpack(
                ">IIBBBBB", payload)
            if (not 1 <= width <= MAX_IMAGE_DIMENSION or not 1 <= height <= MAX_IMAGE_DIMENSION or
                    bit_depth != PNG_BIT_DEPTH_8 or color_type not in PNG_SUPPORTED_COLOR_TYPES or
                    compression != 0 or filtering != 0 or interlace != PNG_INTERLACE_NONE):
                raise ValueError("PNG surface maps require bounded, non-interlaced 8-bit grayscale or truecolor")
            if width * height * RGBA_CHANNEL_COUNT > MAX_DECODED_RGBA_BYTES:
                raise ValueError("PNG decoded pixels exceed the 64 MiB surface-map limit")
            saw_header = True
        elif kind == b"IHDR":
            raise ValueError("PNG contains more than one IHDR chunk")
        elif kind == b"IDAT":
            if data_ended or saw_end:
                raise ValueError("PNG IDAT chunks must be consecutive and precede IEND")
            saw_data = True
            compressed.extend(payload)
        elif kind == b"IEND":
            if not saw_data or length != 0:
                raise ValueError("PNG IEND is malformed or precedes image data")
            saw_end = True
            offset = end
            if offset != len(data):
                raise ValueError("PNG contains data after IEND")
            break
        else:
            if saw_data:
                data_ended = True
            # PLTE is legal in truecolor PNGs; other unknown critical chunks
            # could change pixel interpretation and are rejected.
            if kind == b"PLTE":
                if (saw_data or color_type in (PNG_COLOR_TYPE_GRAY, PNG_COLOR_TYPE_GRAY_ALPHA) or
                        not 3 <= length <= 768 or length % 3):
                    raise ValueError("PNG palette chunk is malformed")
            elif kind[0] & 0x20 == 0:
                raise ValueError("PNG contains an unsupported critical chunk")
        offset = end

    if not saw_header or not saw_data or not saw_end:
        raise ValueError("PNG is missing required image chunks")

    channels = PNG_CHANNELS_BY_COLOR_TYPE[color_type]
    stride = width * channels
    expected_size = height * (stride + 1)
    inflater = zlib.decompressobj()
    try:
        raw = inflater.decompress(bytes(compressed), expected_size + 1)
    except zlib.error as failure:
        raise ValueError("PNG image data cannot be decompressed") from failure
    if (len(raw) != expected_size or not inflater.eof or inflater.unused_data or
            inflater.unconsumed_tail):
        raise ValueError("PNG decoded size does not match its dimensions")

    rgba = bytearray(width * height * 4)
    previous = bytearray(stride)
    source_offset = 0
    output_offset = 0
    for _ in range(height):
        filter_type = raw[source_offset]
        source_offset += 1
        row = bytearray(raw[source_offset:source_offset + stride])
        source_offset += stride
        if filter_type > PNG_MAX_FILTER:
            raise ValueError("PNG scanline has an unsupported filter")
        for index in range(stride):
            left = row[index - channels] if index >= channels else 0
            above = previous[index]
            upper_left = previous[index - channels] if index >= channels else 0
            if filter_type == PNG_FILTER_SUB:
                predictor = left
            elif filter_type == PNG_FILTER_UP:
                predictor = above
            elif filter_type == PNG_FILTER_AVERAGE:
                predictor = (left + above) // 2
            elif filter_type == PNG_FILTER_PAETH:
                estimate = left + above - upper_left
                distance_left = abs(estimate - left)
                distance_above = abs(estimate - above)
                distance_upper_left = abs(estimate - upper_left)
                predictor = left if distance_left <= distance_above and distance_left <= distance_upper_left \
                    else above if distance_above <= distance_upper_left else upper_left
            else:  # PNG_FILTER_NONE
                predictor = 0
            row[index] = (row[index] + predictor) & 0xFF
        for pixel in range(width):
            source = pixel * channels
            destination = output_offset + pixel * RGBA_CHANNEL_COUNT
            if color_type == PNG_COLOR_TYPE_GRAY:
                gray = row[source]
                rgba[destination + RGBA_RED_CHANNEL] = gray
                rgba[destination + RGBA_GREEN_CHANNEL] = gray
                rgba[destination + RGBA_BLUE_CHANNEL] = gray
                rgba[destination + RGBA_ALPHA_CHANNEL] = OPAQUE_ALPHA
            elif color_type == PNG_COLOR_TYPE_RGB:
                rgba[destination + RGBA_RED_CHANNEL] = row[source + RGBA_RED_CHANNEL]
                rgba[destination + RGBA_GREEN_CHANNEL] = row[source + RGBA_GREEN_CHANNEL]
                rgba[destination + RGBA_BLUE_CHANNEL] = row[source + RGBA_BLUE_CHANNEL]
                rgba[destination + RGBA_ALPHA_CHANNEL] = OPAQUE_ALPHA
            elif color_type == PNG_COLOR_TYPE_GRAY_ALPHA:
                gray = row[source]
                rgba[destination + RGBA_RED_CHANNEL] = gray
                rgba[destination + RGBA_GREEN_CHANNEL] = gray
                rgba[destination + RGBA_BLUE_CHANNEL] = gray
                rgba[destination + RGBA_ALPHA_CHANNEL] = row[source + RGBA_GREEN_CHANNEL]
            else:
                rgba[destination:destination + RGBA_CHANNEL_COUNT] = row[source:source + RGBA_CHANNEL_COUNT]
        previous = row
        output_offset += width * RGBA_CHANNEL_COUNT
    return width, height, bytes(rgba)


def pack_surface_maps(roughness_png: bytes | None, metalness_png: bytes | None) -> bytes:
    """Pack source red channels as runtime RGBA: R=AO=255, G=rough, B=metal, A=reflectance=255."""
    if roughness_png is None and metalness_png is None:
        raise ValueError("at least one separate FBX surface map is required")
    roughness = decode_png_rgba(roughness_png) if roughness_png is not None else None
    metalness = decode_png_rgba(metalness_png) if metalness_png is not None else None
    reference = roughness if roughness is not None else metalness
    assert reference is not None
    width, height, _ = reference
    for image in (roughness, metalness):
        if image is not None and image[:2] != (width, height):
            raise ValueError("separate FBX roughness and metalness PNG dimensions must match")
    rough_pixels = roughness[2] if roughness is not None else None
    metal_pixels = metalness[2] if metalness is not None else None
    packed = bytearray(width * height * RGBA_CHANNEL_COUNT)
    for pixel in range(width * height):
        target = pixel * RGBA_CHANNEL_COUNT
        packed[target + RGBA_RED_CHANNEL] = NEUTRAL_SURFACE_CHANNEL
        packed[target + RGBA_GREEN_CHANNEL] = (
            rough_pixels[target + RGBA_RED_CHANNEL] if rough_pixels is not None else NEUTRAL_SURFACE_CHANNEL)
        packed[target + RGBA_BLUE_CHANNEL] = (
            metal_pixels[target + RGBA_RED_CHANNEL] if metal_pixels is not None else NEUTRAL_SURFACE_CHANNEL)
        packed[target + RGBA_ALPHA_CHANNEL] = OPAQUE_ALPHA
    return encode_png(width, height, bytes(packed))
