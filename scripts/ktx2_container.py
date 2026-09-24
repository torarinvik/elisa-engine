"""Bounded structural validation for the Basis KTX2 images the engine accepts."""

from __future__ import annotations

import math
import struct


KTX2_IDENTIFIER = b"\xabKTX 20\xbb\r\n\x1a\n"
KTX2_ETC1S_DFD_MODEL = 163
KTX2_UASTC_LDR_4X4_DFD_MODEL = 166
# Pinned Basis Universal writes UASTC HDR 4x4 using Vulkan's ASTC HDR 4x4
# format ID and the matching UASTC HDR DFD model.
KTX2_UASTC_HDR_4X4_VK_FORMAT = 1000066000
KTX2_UASTC_HDR_4X4_DFD_MODEL = 167
MAX_KTX2_DIMENSION = 4096
MAX_KTX2_LEVELS = 16
_DFD_LENGTHS = (44, 60)
_DFD_SAMPLE_BYTES = 16
_DFD_BASE_BYTES = 28
_DFD_PRIMARIES_BT709 = 1
_DFD_TRANSFER_LINEAR = 1
_DFD_TRANSFER_SRGB = 2
_UASTC_CHANNELS = {0, 3, 4, 5, 6}
_ETC1S_CHANNEL_PAIRS = {(0,), (0, 15), (3,), (3, 4)}


def _sample(data: bytes, dfd_offset: int, index: int) -> tuple[int, int, int, bytes, int, int]:
    offset = dfd_offset + _DFD_BASE_BYTES + index * _DFD_SAMPLE_BYTES
    bit_offset, bit_length, channel_type = struct.unpack_from("<HBB", data, offset)
    position = data[offset + 4:offset + 8]
    lower, upper = struct.unpack_from("<II", data, offset + 8)
    return bit_offset, bit_length, channel_type, position, lower, upper


def _valid_dfd(data: bytes, offset: int, length: int, vk_format: int,
        scheme: int) -> bool:
    if (length not in _DFD_LENGTHS or offset + length > len(data) or
            data[offset + 4:offset + 8] != bytes(4) or
            struct.unpack_from("<HH", data, offset + 8) != (2, length - 4)):
        return False
    model, primaries, transfer, flags = data[offset + 12:offset + 16]
    dimensions = data[offset + 16:offset + 20]
    planes = data[offset + 20:offset + 28]
    if (primaries != _DFD_PRIMARIES_BT709 or
            transfer not in (_DFD_TRANSFER_LINEAR, _DFD_TRANSFER_SRGB) or flags != 0 or
            dimensions != bytes((3, 3, 0, 0))):
        return False
    sample_count = (length - _DFD_BASE_BYTES) // _DFD_SAMPLE_BYTES
    if vk_format == 0 and model == KTX2_UASTC_LDR_4X4_DFD_MODEL:
        if (length != 44 or scheme not in (0, 2) or
                any(planes[1:]) or
                (scheme == 0 and planes[0] != 16) or
                (scheme == 2 and planes[0] not in (0, 16))):
            return False
        bit_offset, bit_length, channel_type, position, lower, upper = _sample(data, offset, 0)
        return (sample_count == 1 and bit_offset == 0 and bit_length == 127 and
            channel_type in _UASTC_CHANNELS and position == bytes(4) and
            lower == 0 and upper == 0xFFFFFFFF)
    if vk_format == 0 and model == KTX2_ETC1S_DFD_MODEL:
        expected_planes = (8, 8) if sample_count == 2 else (8, 0)
        if (scheme != 1 or sample_count not in (1, 2) or
                (length == 44) != (sample_count == 1) or
                any(planes[2:]) or tuple(planes[:2]) not in (expected_planes, (0, 0))):
            return False
        channels: list[int] = []
        for index in range(sample_count):
            bit_offset, bit_length, channel_type, position, lower, upper = _sample(data, offset, index)
            channel = channel_type & 0x0F
            if (bit_offset != index * 64 or bit_length != 63 or
                    channel_type != channel or position != bytes(4) or
                    lower != 0 or upper != 0xFFFFFFFF):
                return False
            channels.append(channel)
        return tuple(channels) in _ETC1S_CHANNEL_PAIRS
    if (vk_format != KTX2_UASTC_HDR_4X4_VK_FORMAT or
            model != KTX2_UASTC_HDR_4X4_DFD_MODEL or scheme != 2 or
            transfer != _DFD_TRANSFER_LINEAR or length != 44 or
            any(planes[1:]) or planes[0] not in (0, 16) or sample_count != 1):
        return False
    bit_offset, bit_length, channel_type, position, lower, upper = _sample(data, offset, 0)
    return (bit_offset == 0 and bit_length == 127 and channel_type == 0x80 and
        position == bytes(4) and lower == 0 and upper == 0x3F800000)


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


def with_key_value(data: bytes, key: str, value: bytes) -> bytes:
    """Return a validated KTX2 with one sorted KVD entry added or replaced."""
    if (not key or "\0" in key or len(key.encode("utf-8")) > 255 or
            not isinstance(value, bytes) or len(value) > 4096 or
            ktx2_dimensions(data) is None):
        raise ValueError("KTX2 key-value update has invalid input")
    width, height = struct.unpack_from("<2I", data, 20)
    levels = struct.unpack_from("<I", data, 40)[0]
    scheme = struct.unpack_from("<I", data, 44)[0]
    dfd_offset, dfd_length, kvd_offset, kvd_length, sgd_offset, sgd_length = \
        struct.unpack_from("<4I2Q", data, 48)
    dfd_end = dfd_offset + dfd_length
    entries: dict[str, bytes] = {}
    position = kvd_offset
    end = kvd_offset + kvd_length
    while position < end:
        entry_length = struct.unpack_from("<I", data, position)[0]
        position += 4
        entry_end = position + entry_length
        entry = data[position:entry_end]
        terminator = entry.index(b"\0")
        entry_key = entry[:terminator].decode("utf-8", errors="strict")
        entries[entry_key] = entry[terminator + 1:]
        position = entry_end + ((-entry_end) & 3)
    entries[key] = value

    new_kvd = bytearray()
    for entry_key, entry_value in sorted(entries.items()):
        encoded_key = entry_key.encode("utf-8") + b"\0"
        entry = encoded_key + entry_value
        new_kvd.extend(struct.pack("<I", len(entry)))
        new_kvd.extend(entry)
        new_kvd.extend(bytes((-len(new_kvd)) & 3))

    level_offsets = [struct.unpack_from("<Q", data, 80 + level * 24)[0]
        for level in range(levels)]
    following_sections = ([sgd_offset] if sgd_length else []) + level_offsets
    first_payload = min(following_sections)
    kvd_start = kvd_offset if kvd_length else dfd_end
    old_kvd_end = kvd_offset + kvd_length if kvd_length else dfd_end
    if first_payload < old_kvd_end:
        raise ValueError("KTX2 key-value section overlaps payload")

    if sgd_length and sgd_offset == first_payload:
        alignment = 8
    else:
        alignment = 1 if scheme != 0 else math.lcm(4, data[dfd_offset + 20])
    new_kvd_end = kvd_start + len(new_kvd)
    new_first_payload = ((new_kvd_end + alignment - 1) // alignment) * alignment
    inserted = bytes(new_kvd) + bytes(new_first_payload - new_kvd_end)
    shift = new_first_payload - first_payload
    updated = bytearray(data[:kvd_start] + inserted + data[first_payload:])

    new_sgd_offset = sgd_offset + shift if sgd_length and sgd_offset >= first_payload else sgd_offset
    struct.pack_into("<4I2Q", updated, 48, dfd_offset, dfd_length,
        kvd_start, len(new_kvd), new_sgd_offset, sgd_length)
    for level in range(levels):
        offset_position = 80 + level * 24
        offset = struct.unpack_from("<Q", updated, offset_position)[0]
        if offset >= first_payload:
            struct.pack_into("<Q", updated, offset_position, offset + shift)
    result = bytes(updated)
    if ktx2_dimensions(result) != (width, height):
        raise ValueError("KTX2 key-value update did not preserve a valid container")
    return result


def ktx2_dimensions(data: bytes) -> tuple[int, int] | None:
    """Validate a bounded 2D Basis KTX2 container and return its dimensions."""
    if len(data) < 80 or data[:12] != KTX2_IDENTIFIER:
        return None
    vk_format, type_size = struct.unpack_from("<2I", data, 12)
    width, height, depth, layers, faces, levels, scheme = struct.unpack_from("<7I", data, 20)
    if (type_size != 1 or
            not 1 <= width <= MAX_KTX2_DIMENSION or not 1 <= height <= MAX_KTX2_DIMENSION or
            depth != 0 or layers != 0 or faces != 1 or not 1 <= levels <= MAX_KTX2_LEVELS or
            levels > max(width, height).bit_length() or scheme not in (0, 1, 2)):
        return None

    level_index_end = 80 + levels * 24
    if level_index_end > len(data):
        return None
    dfd_offset, dfd_length, kvd_offset, kvd_length, sgd_offset, sgd_length = \
        struct.unpack_from("<4I2Q", data, 48)
    if (dfd_offset != level_index_end or dfd_offset & 3 or
            dfd_length > len(data) - dfd_offset):
        return None
    dfd_end = dfd_offset + dfd_length
    if (struct.unpack_from("<I", data, dfd_offset)[0] != dfd_length or
            not _valid_dfd(data, dfd_offset, dfd_length, vk_format, scheme)):
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
    level_offsets: list[int] = []
    level_lengths: list[int] = []
    level_alignment = math.lcm(4, data[dfd_offset + 20]) if scheme == 0 else 1
    for level in range(levels):
        offset, byte_length, uncompressed_length = struct.unpack_from("<3Q", data, 80 + level * 24)
        mip_width = max(1, width >> level)
        mip_height = max(1, height >> level)
        expected_uastc_length = ((mip_width + 3) // 4) * ((mip_height + 3) // 4) * 16
        if (offset < metadata_end or offset > len(data) or byte_length == 0 or
                byte_length > len(data) - offset or offset % level_alignment):
            return None
        if ((scheme in (0, 2) and uncompressed_length == 0) or
                (scheme == 0 and uncompressed_length != byte_length) or
                (scheme == 1 and uncompressed_length != 0) or
                (scheme in (0, 2) and uncompressed_length != expected_uastc_length)):
            return None
        level_offsets.append(offset)
        level_lengths.append(byte_length)
        level_ranges.append((offset, offset + byte_length))

    first_level_offset = ((metadata_end + level_alignment - 1) // level_alignment) * level_alignment
    if (level_offsets[-1] != first_level_offset or
            any(data[metadata_end:first_level_offset])):
        return None
    for level in range(levels - 2, -1, -1):
        previous_end = level_offsets[level + 1] + level_lengths[level + 1]
        expected_offset = ((previous_end + level_alignment - 1) // level_alignment) * level_alignment
        if (level_offsets[level] != expected_offset or
                any(data[previous_end:expected_offset])):
            return None
    level_ranges.sort()
    if any(level_ranges[index][0] < level_ranges[index - 1][1]
            for index in range(1, len(level_ranges))):
        return None
    if level_ranges[-1][1] != len(data):
        return None
    return width, height
