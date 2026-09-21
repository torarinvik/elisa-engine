"""Build deterministic, bounded ELPK version-1 asset bundles."""

from __future__ import annotations

import ctypes
import ctypes.util
import os
from pathlib import Path
import struct
import tempfile
from typing import Mapping, Sequence
import zlib


HEADER_BYTES = 32
ENTRY_BYTES = 48
MAX_PACKAGE_BYTES = 64 * 1024 * 1024
MAX_SECTIONS = 128
MAX_SECTION_BYTES = 64 * 1024 * 1024
MAX_MANIFEST_BYTES = 16 * 1024
MAX_DEPENDENCIES = 16
ALIGNMENT_BYTES = 16
MANIFEST_HEADER = "ELISA-PACKAGE-MANIFEST-1"


def _zstd_library():
    library_name = ctypes.util.find_library("zstd")
    if not library_name:
        return None
    library = ctypes.CDLL(library_name)
    library.ZSTD_compressBound.argtypes = [ctypes.c_size_t]
    library.ZSTD_compressBound.restype = ctypes.c_size_t
    library.ZSTD_compress.argtypes = [ctypes.c_void_p, ctypes.c_size_t,
        ctypes.c_void_p, ctypes.c_size_t, ctypes.c_int]
    library.ZSTD_compress.restype = ctypes.c_size_t
    library.ZSTD_isError.argtypes = [ctypes.c_size_t]
    library.ZSTD_isError.restype = ctypes.c_uint
    return library


def _zstd_compress(data: bytes, level: int) -> bytes:
    library = _zstd_library()
    if library is None:
        # Python 3.14+ ships zstd in the standard library, which covers hosts
        # (such as Windows CI) where libzstd is not on the loader path.
        try:
            from compression import zstd
        except ImportError:
            raise RuntimeError("libzstd or Python 3.14 compression.zstd is required "
                "to build compressed ELPK packages") from None
        return zstd.compress(data, level)
    bound = int(library.ZSTD_compressBound(len(data)))
    destination = ctypes.create_string_buffer(bound)
    source = ctypes.create_string_buffer(data)
    result = int(library.ZSTD_compress(destination, bound, source, len(data), level))
    if library.ZSTD_isError(result):
        raise RuntimeError("libzstd could not compress an ELPK section")
    return destination.raw[:result]


def _safe_section_name(name: str) -> bool:
    if not name or len(name.encode("ascii", errors="ignore")) != len(name) or len(name) > 15:
        return False
    return all("a" <= character <= "z" or "0" <= character <= "9" or character == "_"
        for character in name)


def _safe_logical_path(value: str) -> bool:
    if not value or value.startswith("/") or "\\" in value or "\0" in value:
        return False
    return all(part not in ("", ".", "..") for part in value.split("/"))


def _manifest_bytes(dependencies: Sequence[str]) -> bytes:
    if len(dependencies) > MAX_DEPENDENCIES or len(set(dependencies)) != len(dependencies):
        raise ValueError("package dependency count or uniqueness rejected")
    if any(not _safe_logical_path(value) for value in dependencies):
        raise ValueError("package dependency path is unsafe")
    ordered = sorted(dependencies)
    text = MANIFEST_HEADER + "\n" + "".join(f"dependency={value}\n" for value in ordered)
    encoded = text.encode("ascii")
    if len(encoded) > MAX_MANIFEST_BYTES or any(len(line) > 255 for line in text.splitlines()):
        raise ValueError("package dependency manifest exceeds its bound")
    return encoded


def build_package_bytes(sections: Mapping[str, bytes], dependencies: Sequence[str] = (),
    compression_level: int = 3) -> bytes:
    """Return a deterministic ELPK bundle with a validated dependency manifest."""
    if not -5 <= compression_level <= 22:
        raise ValueError("zstd compression level must be in [-5, 22]")
    if "manifest" in sections:
        raise ValueError("manifest is generated from dependencies and cannot be overridden")
    if not sections or len(sections) + 1 > MAX_SECTIONS:
        raise ValueError("package section count rejected")

    manifest = _manifest_bytes(dependencies)
    payloads: list[tuple[str, bytes, bytes, int]] = []
    for name in sorted(sections):
        if not _safe_section_name(name):
            raise ValueError(f"invalid package section name: {name!r}")
        payload = sections[name]
        if not isinstance(payload, bytes) or not payload or len(payload) > MAX_SECTION_BYTES:
            raise ValueError(f"package section size rejected: {name}")
        compressed = _zstd_compress(payload, compression_level)
        if len(compressed) < len(payload):
            payloads.append((name, compressed, payload, 1))
        else:
            payloads.append((name, payload, payload, 0))
    payloads.append(("manifest", manifest, manifest, 0))
    payloads.sort(key=lambda item: item[0])

    count = len(payloads)
    index_size = count * ENTRY_BYTES
    index_offset = HEADER_BYTES
    payload_offset = HEADER_BYTES + index_size
    if payload_offset > MAX_PACKAGE_BYTES:
        raise ValueError("package index exceeds its size bound")
    entries: list[tuple[str, int, bytes, bytes, int]] = []
    for name, stored, unpacked, compression in payloads:
        payload_offset = (payload_offset + ALIGNMENT_BYTES - 1) & ~(ALIGNMENT_BYTES - 1)
        entries.append((name, payload_offset, stored, unpacked, compression))
        payload_offset += len(stored)
        if payload_offset > MAX_PACKAGE_BYTES:
            raise ValueError("package exceeds its 64 MiB reader limit")

    package = bytearray(payload_offset)
    struct.pack_into("<4sHHQQQ", package, 0, b"ELPK", 1, count,
        index_offset, index_size, 0)
    for index, (name, offset, stored, unpacked, compression) in enumerate(entries):
        entry_offset = HEADER_BYTES + index * ENTRY_BYTES
        encoded_name = name.encode("ascii") + b"\0"
        struct.pack_into("<16sQQQII", package, entry_offset, encoded_name,
            offset, len(stored), len(unpacked), compression, zlib.crc32(unpacked) & 0xFFFFFFFF)
        package[offset:offset + len(stored)] = stored
    return bytes(package)


def write_package(path: Path, sections: Mapping[str, bytes], dependencies: Sequence[str] = (),
    compression_level: int = 3) -> None:
    """Atomically write a deterministic ELPK package beside the destination."""
    package = build_package_bytes(sections, dependencies, compression_level)
    path = path.expanduser().resolve()
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary_name = ""
    try:
        with tempfile.NamedTemporaryFile(prefix=f".{path.name}.", suffix=".tmp",
                dir=path.parent, delete=False) as output:
            temporary_name = output.name
            output.write(package)
            output.flush()
            os.fsync(output.fileno())
        # NamedTemporaryFile is owner-only; published bundles are ordinary readable files.
        os.chmod(temporary_name, 0o644)
        os.replace(temporary_name, path)
    finally:
        if temporary_name and os.path.exists(temporary_name):
            os.unlink(temporary_name)


def write_geometry_package(path: Path, geometry_package: bytes) -> None:
    """Wrap the normalized cooked geometry package in an ELPK mesh section."""
    write_package(path, {"mesh": geometry_package})
