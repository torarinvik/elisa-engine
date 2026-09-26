"""Check the CPU PNG writer's dimensions, channel order, and row pitch."""

from pathlib import Path
import os
import struct
import subprocess
import tempfile
import zlib


ROOT = Path(__file__).resolve().parent.parent


def main() -> int:
    compiler = os.environ.get("CXX", "clang++")
    harness = r'''
#include "native/rgba_png.h"
#include <cstdint>
#include <cstring>
#include <vector>
int main(int argc, char** argv) {
    const std::vector<uint8_t> bgra = {
        3, 2, 1, 255, 6, 5, 4, 255, 99, 99, 99, 99,
        9, 8, 7, 255, 12, 11, 10, 255
    };
    if (!elisa::capture::save_rgba_png(bgra.data(), bgra.size(), 2, 2, 12,
            elisa::capture::PixelOrder::BGRA, argv[1])) return 1;
    if (elisa::capture::save_rgba_png(bgra.data(), bgra.size() - 1, 2, 2, 12,
            elisa::capture::PixelOrder::BGRA, argv[1])) return 2;
    if (elisa::capture::save_rgba_png(bgra.data(), bgra.size(), 2, 2, 7,
            elisa::capture::PixelOrder::BGRA, argv[1])) return 3;
    if (elisa::capture::save_rgba_png(nullptr, 0, 2, 2, 8,
            elisa::capture::PixelOrder::RGBA, argv[1])) return 4;
    const uint32_t packed = 1023u | (512u << 10) | (3u << 30);
    if (!elisa::capture::save_rgba_png(reinterpret_cast<const uint8_t*>(&packed), 4, 1, 1, 4,
            elisa::capture::PixelOrder::RGB10A2, argv[2])) return 5;
    return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix="elisa-rgba-png-") as temporary:
        root = Path(temporary)
        source = root / "harness.cpp"
        binary = root / "harness"
        image = root / "capture.png"
        packed_image = root / "packed.png"
        source.write_text(harness, encoding="utf-8")
        subprocess.run([compiler, "-std=c++17", "-I", str(ROOT), str(source), "-o", str(binary)], check=True)
        subprocess.run([str(binary), str(image), str(packed_image)], check=True)
        encoded = image.read_bytes()
        packed_encoded = packed_image.read_bytes()

    if encoded[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("invalid PNG signature")
    offset = 8
    chunks = {}
    while offset < len(encoded):
        length = struct.unpack_from(">I", encoded, offset)[0]
        kind = encoded[offset + 4:offset + 8]
        data = encoded[offset + 8:offset + 8 + length]
        crc = struct.unpack_from(">I", encoded, offset + 8 + length)[0]
        import binascii

        if binascii.crc32(kind + data) & 0xFFFFFFFF != crc:
            raise ValueError(f"bad CRC in {kind!r}")
        chunks.setdefault(kind, []).append(data)
        offset += 12 + length
    if struct.unpack(">IIBBBBB", chunks[b"IHDR"][0]) != (2, 2, 8, 6, 0, 0, 0):
        raise ValueError("unexpected PNG header")
    pixels = zlib.decompress(b"".join(chunks[b"IDAT"]))
    expected = bytes((0, 1, 2, 3, 255, 4, 5, 6, 255,
                      0, 7, 8, 9, 255, 10, 11, 12, 255))
    if pixels != expected:
        raise ValueError(f"unexpected RGBA scanlines: {pixels!r}")
    if chunks.get(b"IEND") != [b""] or offset != len(encoded):
        raise ValueError("missing IEND or trailing bytes")
    packed_offset = 8
    packed_data = bytearray()
    while packed_offset < len(packed_encoded):
        chunk_size = struct.unpack_from(">I", packed_encoded, packed_offset)[0]
        chunk_type = packed_encoded[packed_offset + 4:packed_offset + 8]
        chunk_data = packed_encoded[packed_offset + 8:packed_offset + 8 + chunk_size]
        if chunk_type == b"IDAT":
            packed_data.extend(chunk_data)
        packed_offset += 12 + chunk_size
    if zlib.decompress(packed_data) != bytes((0, 255, 127, 0, 255)):
        raise ValueError("unexpected R10G10B10A2 conversion")
    print("RGBA PNG self-test passed (BGRA swap, packed 10-bit, padded rows, bounds, CRC, pixels).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
