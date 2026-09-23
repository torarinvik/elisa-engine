"""Small KTX2 section-layout fixtures for cooker-boundary tests."""

from __future__ import annotations

import struct

from ktx2_container import KTX2_IDENTIFIER


def make_ktx2(width: int = 4, height: int = 4, *, scheme: int = 0,
        key_values: tuple[tuple[str, bytes], ...] = (), global_data: bytes = b"",
        levels: tuple[bytes, ...] = (b"KTX!",)) -> bytes:
    """Build a bounded section-layout fixture; image and DFD data are placeholders."""
    dfd = bytearray(44)
    struct.pack_into("<I", dfd, 0, len(dfd))
    struct.pack_into("<HH", dfd, 8, 2, len(dfd) - 4)
    kvd = bytearray()
    for key, value in key_values:
        entry = key.encode("utf-8") + b"\0" + value
        kvd.extend(struct.pack("<I", len(entry)))
        kvd.extend(entry)
        kvd.extend(bytes(-len(kvd) % 4))

    level_index_end = 80 + len(levels) * 24
    dfd_offset = level_index_end
    kvd_offset = dfd_offset + len(dfd) if kvd else 0
    position = dfd_offset + len(dfd) + len(kvd)
    sgd_offset = 0
    if global_data:
        sgd_offset = (position + 7) & ~7
        position = sgd_offset + len(global_data)
    file_data = bytearray(position)
    file_data[:12] = KTX2_IDENTIFIER
    struct.pack_into("<9I", file_data, 12,
        0, 1, width, height, 0, 0, 1, len(levels), scheme)
    struct.pack_into("<4I2Q", file_data, 48,
        dfd_offset, len(dfd), kvd_offset, len(kvd), sgd_offset, len(global_data))
    file_data[dfd_offset:dfd_offset + len(dfd)] = dfd
    if kvd:
        file_data[kvd_offset:kvd_offset + len(kvd)] = kvd
    if global_data:
        file_data[sgd_offset:sgd_offset + len(global_data)] = global_data

    index_position = 80
    for level_data in levels:
        offset = (position + 3) & ~3
        if offset > len(file_data):
            file_data.extend(bytes(offset - len(file_data)))
        file_data.extend(level_data)
        uncompressed = 0 if scheme == 1 else len(level_data)
        struct.pack_into("<3Q", file_data, index_position, offset,
            len(level_data), uncompressed)
        index_position += 24
        position = offset + len(level_data)
    return bytes(file_data)
