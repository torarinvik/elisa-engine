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
#include <cstdlib>
#include <new>
size_t largest = 0;
void* operator new(size_t size) { largest = std::max(largest, size); void* p = std::malloc(size); if (!p) throw std::bad_alloc(); return p; }
void operator delete(void* p) noexcept { std::free(p); }
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
    if (!elisa::capture::save_rgba_png(reinterpret_cast<const uint8_t*>(&packed), 4, 1, 1, 4,
            elisa::capture::PixelOrder::BGR10A2, argv[3])) return 6;
    for (int fixture = 0; fixture < 3; ++fixture) {
        const uint32_t width = fixture == 0 ? 5461 : (fixture == 1 ? 1024 : 2560);
        const uint32_t height = fixture == 0 ? 3 : (fixture == 1 ? 40 : 1440);
        std::vector<uint8_t> raw(size_t(width) * height * 4);
        for (size_t i = 0; i < raw.size(); ++i) raw[i] = fixture == 2 ? 255 : uint8_t(i % 251);
        largest = 0;
        if (!elisa::capture::save_rgba_png(raw.data(), raw.size(), width, height,
            size_t(width) * 4, elisa::capture::PixelOrder::RGBA, argv[4 + fixture])) return 7;
        if (largest > 65535) return 8;
    }
    return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix="elisa-rgba-png-") as temporary:
        root = Path(temporary)
        source = root / "harness.cpp"
        binary = root / "harness"
        image = root / "capture.png"
        packed_image = root / "packed.png"
        metal_packed_image = root / "metal-packed.png"
        block_images = [root / "exact-block.png", root / "multi-block.png", root / "full-size.png"]
        source.write_text(harness, encoding="utf-8")
        subprocess.run([compiler, "-std=c++17", "-I", str(ROOT), str(source), "-o", str(binary)], check=True)
        subprocess.run([str(binary), str(image), str(packed_image), str(metal_packed_image), *map(str, block_images)], check=True)
        encoded = image.read_bytes()
        packed_encoded = packed_image.read_bytes()
        metal_packed_encoded = metal_packed_image.read_bytes()
        block_encoded = [path.read_bytes() for path in block_images]

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
    def idat_pixels(encoded_png: bytes) -> bytes:
        packed_offset = 8
        packed_data = bytearray()
        while packed_offset < len(encoded_png):
            chunk_size = struct.unpack_from(">I", encoded_png, packed_offset)[0]
            chunk_type = encoded_png[packed_offset + 4:packed_offset + 8]
            if chunk_type == b"IDAT":
                packed_data.extend(encoded_png[packed_offset + 8:packed_offset + 8 + chunk_size])
            packed_offset += 12 + chunk_size
        return zlib.decompress(packed_data)

    if idat_pixels(packed_encoded) != bytes((0, 255, 127, 0, 255)):
        raise ValueError("unexpected R10G10B10A2 conversion")
    if idat_pixels(metal_packed_encoded) != bytes((0, 0, 127, 255, 255)):
        raise ValueError("unexpected BGR10A2 conversion")
    for data, width, height in zip(block_encoded, (5461, 1024, 2560), (3, 40, 1440)):
        raw = bytes([255]) * (width * height * 4) if width == 2560 else bytes(i % 251 for i in range(width * height * 4))
        expected = b"".join(b"\0" + raw[row * width * 4:(row + 1) * width * 4] for row in range(height))
        if idat_pixels(data) != expected:
            raise ValueError("stored-block boundary corrupted scanlines")
        offset = 8
        while offset < len(data):
            size = struct.unpack_from(">I", data, offset)[0]
            chunk = data[offset + 4:offset + 8 + size]
            crc = struct.unpack_from(">I", data, offset + 8 + size)[0]
            if binascii.crc32(chunk) & 0xFFFFFFFF != crc:
                raise ValueError("bad streaming chunk CRC")
            offset += size + 12
    print("RGBA PNG self-test passed (BGRA swap, packed RGB/BGR 10-bit, padded rows, bounds, CRC, pixels, block boundaries, full-size maximum-byte Adler sums, largest encoder allocation <= 64 KiB).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
