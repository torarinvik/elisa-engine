"""Bounded structural validation for the Basis KTX2 images the engine accepts."""

from __future__ import annotations

import struct


KTX2_IDENTIFIER = b"\xabKTX 20\xbb\r\n\x1a\n"
MAX_KTX2_DIMENSION = 4096
MAX_KTX2_LEVELS = 16
_DFD_LENGTHS = (44, 60)


def _valid_key_values(data: bytes, start: int, length: int) -> bool:
    end = start + length
    position = start
    previous_key = ""
    seen: set[str] = set()
    while position < end:
        if end - position < 4:
            return False
        entry_length = struct.unpack_from("<I", data, position)[0]
        position += 4
        if entry_length < 2 or entry_length > end - position:
            return False
        entry_end = position + entry_length
        entry = data[position:entry_end]
        terminator = entry.find(b"\0")
        if terminator <= 0:
            return False
        try:
            key = entry[:terminator].decode("utf-8", errors="strict")
        except UnicodeDecodeError:
            return False
        if key in seen or key < previous_key:
            return False
        seen.add(key)
        previous_key = key
        position = entry_end
        padding = (-position) & 3
        if padding > end - position or any(data[position:position + padding]):
            return False
        position += padding
    return position == end


def ktx2_dimensions(data: bytes) -> tuple[int, int] | None:
    """Validate a bounded 2D Basis KTX2 container and return its dimensions."""
    if len(data) < 80 or data[:12] != KTX2_IDENTIFIER:
        return None
    vk_format, type_size = struct.unpack_from("<2I", data, 12)
    width, height, depth, layers, faces, levels, scheme = struct.unpack_from("<7I", data, 20)
    if (vk_format != 0 or type_size != 1 or
            not 1 <= width <= MAX_KTX2_DIMENSION or not 1 <= height <= MAX_KTX2_DIMENSION or
            depth != 0 or layers != 0 or faces != 1 or not 1 <= levels <= MAX_KTX2_LEVELS or
            scheme not in (0, 1, 2)):
        return None

    level_index_end = 80 + levels * 24
    if level_index_end > len(data):
        return None
    dfd_offset, dfd_length, kvd_offset, kvd_length, sgd_offset, sgd_length = \
        struct.unpack_from("<4I2Q", data, 48)
    if (dfd_offset != level_index_end or dfd_offset & 3 or dfd_length not in _DFD_LENGTHS or
            dfd_length > len(data) - dfd_offset):
        return None
    dfd_end = dfd_offset + dfd_length
    if (struct.unpack_from("<I", data, dfd_offset)[0] != dfd_length or
            struct.unpack_from("<H", data, dfd_offset + 10)[0] != dfd_length - 4):
        return None

    if kvd_length == 0:
        if kvd_offset != 0:
            return None
        kvd_end = dfd_end
    else:
        if kvd_offset != dfd_end or kvd_offset & 3 or kvd_length > len(data) - kvd_offset:
            return None
        if not _valid_key_values(data, kvd_offset, kvd_length):
            return None
        kvd_end = kvd_offset + kvd_length

    metadata_end = kvd_end
    if scheme == 1:
        expected_sgd_offset = (kvd_end + 7) & ~7
        if (sgd_length == 0 or sgd_offset != expected_sgd_offset or sgd_offset & 7 or
                sgd_offset > len(data) or sgd_length > len(data) - sgd_offset or
                any(data[kvd_end:sgd_offset])):
            return None
        metadata_end = sgd_offset + sgd_length
    elif sgd_offset != 0 or sgd_length != 0:
        return None

    level_ranges: list[tuple[int, int]] = []
    for level in range(levels):
        offset, byte_length, uncompressed_length = struct.unpack_from("<3Q", data, 80 + level * 24)
        if (offset < metadata_end or offset > len(data) or byte_length == 0 or
                byte_length > len(data) - offset):
            return None
        if ((scheme in (0, 2) and uncompressed_length == 0) or
                (scheme == 0 and uncompressed_length != byte_length) or
                (scheme == 1 and uncompressed_length != 0)):
            return None
        level_ranges.append((offset, offset + byte_length))

    level_ranges.sort()
    if any(level_ranges[index][0] < level_ranges[index - 1][1]
            for index in range(1, len(level_ranges))):
        return None
    return width, height
