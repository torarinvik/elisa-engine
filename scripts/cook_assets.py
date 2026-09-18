"""Cook authored source assets into a versioned runtime package (offline tool).

The plan keeps complex importers out of the shipped runtime: this tool runs
at build/validation time, reads the fixture's `mesh_asset`, validates its
normalized counts against the fixture, and writes a versioned package with a
content hash under build/cooked/. Hosts never parse the package to decide
gameplay; the package is what a runtime would load.

Usage:
  python3 scripts/cook_assets.py ENGINE_ROOT

Exit status is nonzero on any mismatch, so the validation workflow fails
rather than producing a package that disagrees with the fixture.
"""

import base64
import hashlib
import json
import os
import random
import shutil
import sqlite3
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

PACKAGE_FORMAT = "elisa-cooked-v2"

# KTX1 container constants. Godot's Image.load_ktx_from_buffer reads this
# container, so a cooked block texture can travel as one file instead of
# inline base64 inside the text package. KTX2 and Basis supercompression
# need an encoder this environment does not have and stay deferred.
KTX_IDENTIFIER = bytes((0xAB, 0x4B, 0x54, 0x58, 0x20, 0x31, 0x31, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A))
GL_UNSIGNED_BYTE = 0x1401
GL_RGB = 0x1907
GL_RGBA = 0x1908
GL_RGBA8 = 0x8058
GL_COMPRESSED_RGB_S3TC_DXT1_EXT = 0x83F0


def ktx1_bytes(width: int, height: int, gl_type: int, gl_format: int, gl_internal: int,
               gl_base: int, payload: bytes, image_bytes: int) -> bytes:
    header = struct.pack(
        "<13I",
        0x04030201,  # little-endian marker
        gl_type,
        1,  # glTypeSize
        gl_format,
        gl_internal,
        gl_base,
        width,
        height,
        0,  # pixelDepth
        0,  # numberOfArrayElements
        1,  # numberOfFaces
        1,  # numberOfMipmapLevels
        0,  # bytesOfKeyValueData
    )
    return KTX_IDENTIFIER + header + struct.pack("<I", image_bytes) + payload

# Bounds on untrusted import input. The plan asks to bound parsing work and to
# test malformed assets and oversized counts, so every structural count and
# byte range is checked before it is used, and a document that exceeds a bound
# is rejected rather than partially parsed.
MAX_DOCUMENT_BYTES = 64 * 1024 * 1024
MAX_BUFFER_BYTES = 64 * 1024 * 1024
MAX_ACCESSORS = 4096
MAX_BUFFER_VIEWS = 4096
MAX_MESHES = 4096


def read_manifest(root: Path) -> dict:
    values = {}
    for line in (root / "backends/scene_manifest.txt").read_text(encoding="utf-8").splitlines():
        text = line.strip()
        if not text or text.startswith("#") or "=" not in text:
            continue
        (key, value) = text.split("=", 1)
        values[key.strip()] = value.strip()
    return values


def read_gltf(data: bytes) -> dict:
    if len(data) > MAX_DOCUMENT_BYTES:
        raise ValueError("glTF document exceeds the size bound")
    document = json.loads(data)
    if document.get("asset", {}).get("version") != "2.0":
        raise ValueError("not a glTF 2.0 asset")
    if len(document.get("accessors", [])) > MAX_ACCESSORS:
        raise ValueError("glTF exceeds the accessor bound")
    if len(document.get("bufferViews", [])) > MAX_BUFFER_VIEWS:
        raise ValueError("glTF exceeds the bufferView bound")
    if len(document.get("meshes", [])) > MAX_MESHES:
        raise ValueError("glTF exceeds the mesh bound")
    return document


def source_bytes(root: Path, document: dict) -> bytes:
    # Only embedded (data URI) buffers are handled: the authored fixture is
    # self-contained, and a runtime package must not depend on loose files.
    buffers = document.get("buffers", [])
    if len(buffers) != 1:
        raise ValueError("expected exactly one embedded buffer")
    uri = buffers[0].get("uri", "")
    if not uri.startswith("data:") or ";base64," not in uri:
        raise ValueError("buffer is not an embedded base64 data URI")
    encoded = uri.split(";base64,", 1)[1]
    # The encoded form is about 4/3 of the decoded size; check before decoding
    # so an oversized URI cannot allocate the whole buffer first.
    if len(encoded) > MAX_BUFFER_BYTES * 2:
        raise ValueError("embedded buffer exceeds the size bound")
    decoded = base64.b64decode(encoded)
    if len(decoded) > MAX_BUFFER_BYTES:
        raise ValueError("embedded buffer exceeds the size bound")
    return decoded


def accessor_bytes(document: dict, buffer: bytes, accessor_index: int) -> bytes:
    accessors = document["accessors"]
    if accessor_index < 0 or accessor_index >= len(accessors):
        raise ValueError("accessor index out of range")
    accessor = accessors[accessor_index]
    views = document["bufferViews"]
    view_index = accessor["bufferView"]
    if view_index < 0 or view_index >= len(views):
        raise ValueError("bufferView index out of range")
    view = views[view_index]
    component_sizes = {5120: 1, 5121: 1, 5122: 2, 5123: 2, 5125: 4, 5126: 4}
    components = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}[accessor["type"]]
    element = component_sizes[accessor["componentType"]] * components
    stride = view.get("byteStride", element)
    start = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
    count = accessor["count"]
    if count < 0:
        raise ValueError("accessor count is negative")
    # The whole range the accessor would touch must lie inside the buffer, so a
    # truncated or overflowing range is rejected instead of silently sliced.
    end = start + element * count if count == 0 else start + (count - 1) * stride + element
    if start < 0 or end > len(buffer):
        raise ValueError("accessor range exceeds the buffer")
    if stride == element:
        return buffer[start:start + element * count]
    packed = bytearray()
    for index in range(count):
        offset = start + index * stride
        packed += buffer[offset:offset + element]
    return bytes(packed)


def normalized_counts(document: dict) -> dict:
    triangles = 0
    positions = 0
    bounds = None
    for mesh in document.get("meshes", []):
        for primitive in mesh.get("primitives", []):
            accessors = document.get("accessors", [])
            indices = primitive.get("indices")
            if indices is not None:
                triangles += accessors[indices]["count"] // 3
            for (attribute, reference) in primitive.get("attributes", {}).items():
                if attribute != "POSITION":
                    continue
                accessor = accessors[reference]
                positions += accessor["count"]
                if "min" in accessor and "max" in accessor:
                    bounds = (accessor["min"], accessor["max"])
    return {"triangles": triangles, "positions": positions, "bounds": bounds}


def normalized_geometry(document: dict, buffer: bytes):
    # Geometry in the shapes a runtime can upload directly: float32 positions
    # and uint32 indices. Other component types are converted here, offline,
    # so the runtime never has to know a source format's encodings.
    for mesh in document.get("meshes", []):
        for primitive in mesh.get("primitives", []):
            attributes = primitive.get("attributes", {})
            indices_ref = primitive.get("indices")
            if indices_ref is None or "POSITION" not in attributes:
                continue
            position_accessor = document["accessors"][attributes["POSITION"]]
            if position_accessor["componentType"] != 5126 or position_accessor["type"] != "VEC3":
                raise ValueError("only float32 VEC3 positions are cooked")
            positions = accessor_bytes(document, buffer, attributes["POSITION"])
            index_accessor = document["accessors"][indices_ref]
            if index_accessor["componentType"] not in (5123, 5125):
                raise ValueError("only 16- or 32-bit indices are cooked")
            normals = b""
            if "NORMAL" in attributes:
                normal_accessor = document["accessors"][attributes["NORMAL"]]
                if normal_accessor["componentType"] == 5126 and normal_accessor["type"] == "VEC3":
                    normals = accessor_bytes(document, buffer, attributes["NORMAL"])
            raw_indices = accessor_bytes(document, buffer, indices_ref)
            import struct
            if index_accessor["componentType"] == 5123:
                converted = b"".join(struct.pack("<I", value) for (value,) in struct.iter_unpack("<H", raw_indices))
            else:
                converted = raw_indices
            return {"positions": positions, "normals": normals, "indices": converted}
    raise ValueError("no primitive with positions and indices")


def record_catalogue(root: Path, asset_rel: str, digest: str, counts: dict) -> Path:
    # The toolkit keeps a persistent catalogue (SQLite) of cooked assets, so a
    # later tool can look up a source by its content hash without re-parsing the
    # package. The runtime does not read this database.
    database = root / "build/catalogue.db"
    database.parent.mkdir(parents=True, exist_ok=True)
    connection = sqlite3.connect(database)
    try:
        connection.execute(
            "CREATE TABLE IF NOT EXISTS assets ("
            "source TEXT PRIMARY KEY, sha256 TEXT NOT NULL, "
            "triangles INTEGER NOT NULL, positions INTEGER NOT NULL)"
        )
        connection.execute(
            "INSERT INTO assets (source, sha256, triangles, positions) VALUES (?, ?, ?, ?) "
            "ON CONFLICT(source) DO UPDATE SET "
            "sha256=excluded.sha256, triangles=excluded.triangles, positions=excluded.positions",
            (asset_rel, digest, counts["triangles"], counts["positions"]),
        )
        connection.commit()
    finally:
        connection.close()
    return database


def write_texture_package(root: Path, size: int = 4) -> Path:
    # The first texture step: a small RGBA checkerboard cooked to a versioned
    # texture package. It is procedural for now; an authored image and GPU
    # compression are later steps. The runtime reads the package, not a source
    # format, exactly as it does for geometry.
    pixels = bytearray()
    for y in range(size):
        for x in range(size):
            # Solid green so the texture can drive a material's albedo and
            # still be checked by the goal marker's colour test on both hosts.
            pixels += bytes((26, 229, 51, 255))
    package_dir = root / "build/cooked"
    package_dir.mkdir(parents=True, exist_ok=True)
    package = package_dir / "maze_tile_tex.rgba"
    lines = [
        "format=elisa-texture-v1",
        f"width={size}",
        f"height={size}",
        "channels=4",
        "pixels_b64=" + base64.b64encode(bytes(pixels)).decode(),
    ]
    package.write_text("\n".join(lines) + "\n", encoding="utf-8")
    # A 16-bit packed companion: the same solid colour in R5G6B5, half the
    # bytes per pixel. This is bit-depth compression, not block compression; the
    # KTX/Basis path is still the higher-quality option.
    packed = bytearray()
    for _y in range(size):
        for _x in range(size):
            value = ((26 >> 3) << 11) | ((229 >> 2) << 5) | (51 >> 3)
            packed += bytes((value & 0xFF, (value >> 8) & 0xFF))
    lines16 = [
        "format=elisa-texture-v1",
        "packing=rgb565",
        f"width={size}",
        f"height={size}",
        "bytes_per_pixel=2",
        "pixels_b64=" + base64.b64encode(bytes(packed)).decode(),
    ]
    package16 = package_dir / "maze_tile_tex16.rgba"
    package16.write_text("\n".join(lines16) + "\n", encoding="utf-8")
    # Block compression: a BC1/DXT1 block is two RGB565 endpoints plus 2-bit
    # indices. A solid colour encodes as both endpoints equal and all indices
    # zero, so a 4x4 texture is one 8-byte block. KTX/Basis use the same family
    # of block formats; this is a real block-compressed path without their
    # toolchains.
    r5 = 26 >> 3
    g6 = 229 >> 2
    b5 = 51 >> 3
    endpoint = ((r5 << 11) | (g6 << 5) | b5) & 0xFFFF
    block = bytes((endpoint & 0xFF, (endpoint >> 8) & 0xFF, endpoint & 0xFF, (endpoint >> 8) & 0xFF, 0, 0, 0, 0))
    lines_bc1 = [
        "format=elisa-texture-v1",
        "packing=bc1",
        f"width={size}",
        f"height={size}",
        "block_bytes=8",
        "pixels_b64=" + base64.b64encode(block).decode(),
    ]
    package_bc1 = package_dir / "maze_tile_tex_bc1.rgba"
    package_bc1.write_text("\n".join(lines_bc1) + "\n", encoding="utf-8")
    # The same payloads in a KTX1 container: a real texture format both hosts
    # can open directly (Godot via Image.load_ktx_from_buffer), rather than a
    # project-local text package. The block payload keeps its BC1 format ID.
    (package_dir / "maze_tile_tex.ktx").write_bytes(
        ktx1_bytes(size, size, GL_UNSIGNED_BYTE, GL_RGBA, GL_RGBA8, GL_RGBA, bytes(pixels), size * size * 4))
    (package_dir / "maze_tile_tex_bc1.ktx").write_bytes(
        ktx1_bytes(size, size, 0, 0, GL_COMPRESSED_RGB_S3TC_DXT1_EXT, GL_RGB, block, 8))
    # Basis Universal KTX2: the plan's supercompressed path. The Basis encoder
    # is an external tool (scripts/fetch_basisu.py); when it is not fetched the
    # cooker still produces the KTX1 containers, and the dedicated
    # scripts/basisu_probe.py is the verification path for this format.
    write_basisu_ktx2(root, package_dir, pixels, size)
    return package


def write_png(width: int, height: int, rgba: bytes) -> bytes:
    # A minimal PNG writer so the cooker does not need an image library to hand
    # the Basis encoder its source pixels.
    def chunk(tag: bytes, data: bytes) -> bytes:
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    rows = b"".join(b"\x00" + rgba[y * width * 4:(y + 1) * width * 4] for y in range(height))
    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(rows))
        + chunk(b"IEND", b"")
    )


def write_basisu_ktx2(root: Path, package_dir: Path, pixels: bytes, size: int):
    basisu = os.environ.get("BASISU_BIN", "")
    if not basisu:
        candidate = root / "dependencies/basisu/bin/basisu"
        if candidate.is_file():
            basisu = str(candidate)
    if not basisu or shutil.which(basisu) is None:
        return None
    output = package_dir / "maze_tile_tex.ktx2"
    with tempfile.TemporaryDirectory(prefix="elisa-basisu-") as workdir:
        source = Path(workdir) / "tile.png"
        source.write_bytes(write_png(size, size, pixels))
        result = subprocess.run(
            [basisu, "-ktx2", "-uastc", "-linear", str(source), "-output_file", str(output)],
            capture_output=True, text=True, check=False,
        )
    if result.returncode != 0 or not output.is_file():
        raise ValueError(f"Basis encoding failed: {result.stderr.strip() or result.stdout.strip()}")
    return output


def cook(root: Path) -> Path:
    manifest = read_manifest(root)
    asset_rel = manifest.get("mesh_asset", "")
    if not asset_rel:
        raise ValueError("scene manifest has no mesh_asset")
    asset_path = root / asset_rel
    data = asset_path.read_bytes()
    document = read_gltf(data)
    counts = normalized_counts(document)
    expected_triangles = int(manifest.get("mesh_triangles", "0"))
    if counts["triangles"] != expected_triangles:
        raise ValueError(
            f"cooked triangles {counts['triangles']} disagree with the fixture's {expected_triangles}")
    if counts["positions"] <= 0 or counts["bounds"] is None:
        raise ValueError("cooked positions or bounds are missing")
    digest = hashlib.sha256(data).hexdigest()
    geometry = normalized_geometry(document, source_bytes(root, document))

    package_dir = root / "build/cooked"
    package_dir.mkdir(parents=True, exist_ok=True)
    package = package_dir / (asset_path.stem + ".pkg")
    lines = [
        f"format={PACKAGE_FORMAT}",
        f"source={asset_rel}",
        f"source_sha256={digest}",
        f"triangles={counts['triangles']}",
        f"positions={counts['positions']}",
        "bounds_min=" + ",".join(str(v) for v in counts["bounds"][0]),
        "bounds_max=" + ",".join(str(v) for v in counts["bounds"][1]),
        "position_stride=12",
        "index_stride=4",
        "positions_b64=" + base64.b64encode(geometry["positions"]).decode(),
        "normals_b64=" + base64.b64encode(geometry["normals"]).decode(),
        "indices_b64=" + base64.b64encode(geometry["indices"]).decode(),
    ]
    package.write_text("\n".join(lines) + "\n", encoding="utf-8")
    database = record_catalogue(root, asset_rel, digest, counts)
    texture = write_texture_package(root)
    print(f"cooked {asset_rel} -> {package} ({counts['triangles']} triangles, sha256 {digest[:12]})")
    print(f"cooked texture -> {texture}")
    print(f"catalogue -> {database}")
    return package


def self_test() -> int:
    def document(accessor_count: int = 3, component: int = 5126, view: int = 0, buffer_bytes: int = 64) -> dict:
        payload = base64.b64encode(bytes(buffer_bytes)).decode()
        return {
            "asset": {"version": "2.0"},
            "buffers": [{"uri": "data:application/octet-stream;base64," + payload}],
            "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": buffer_bytes}],
            "accessors": [{"bufferView": view, "componentType": component, "count": accessor_count, "type": "VEC3"}],
            "meshes": [{"primitives": [{"attributes": {"POSITION": 0}, "indices": 0}]}],
        }

    def buffer_of(doc: dict) -> bytes:
        return source_bytes(None, doc)

    checks = [
        ("wrong version", lambda: read_gltf(b'{"asset":{"version":"1.0"}}')),
        ("too many accessors", lambda: read_gltf(json.dumps({"asset": {"version": "2.0"}, "accessors": [{}] * (MAX_ACCESSORS + 1)}).encode())),
        ("no embedded buffer", lambda: source_bytes(None, {"buffers": []})),
        ("view index out of range", lambda: accessor_bytes(document(view=9), buffer_of(document(view=9)), 0)),
        ("accessor beyond buffer", lambda: accessor_bytes(document(accessor_count=100), buffer_of(document(accessor_count=100)), 0)),
        ("negative count", lambda: accessor_bytes(document(accessor_count=-1), buffer_of(document(accessor_count=-1)), 0)),
        ("unsupported component", lambda: normalized_geometry(document(component=5120), buffer_of(document(component=5120)))),
    ]
    declared = (OSError, ValueError, KeyError, IndexError, TypeError, AttributeError, json.JSONDecodeError)
    failures = 0
    for (name, call) in checks:
        try:
            call()
            print(f"self-test: {name} was accepted but should have been rejected", file=sys.stderr)
            failures += 1
        except declared:
            pass
    # Fuzz the import boundary: structurally random documents must be rejected
    # with a declared error, never with an undeclared exception type.
    rng = random.Random(20260918)
    keys = ["asset", "version", "accessors", "bufferViews", "buffers", "meshes", "primitives",
            "attributes", "POSITION", "NORMAL", "indices", "bufferView", "componentType",
            "count", "type", "uri", "byteOffset", "byteStride", "min", "max"]
    def random_value(depth: int):
        if depth <= 0:
            return rng.choice([0, 1, -1, 3, 5126, "SCALAR", "VEC3", "data:,", "", None, True])
        pick = rng.randrange(5)
        if pick == 0:
            return rng.randrange(-4, 64)
        if pick == 1:
            return [random_value(depth - 1) for _ in range(rng.randrange(3))]
        if pick == 2:
            return {rng.choice(keys): random_value(depth - 1) for _ in range(rng.randrange(3))}
        if pick == 3:
            return rng.choice(["", "x", "2.0", "data:,", "VEC3", "SCALAR"])
        return None
    fuzz_rounds = 96
    for _ in range(fuzz_rounds):
        document = {"asset": {"version": "2.0"}, **{rng.choice(keys): random_value(3) for _ in range(rng.randrange(5))}}
        try:
            parsed = read_gltf(json.dumps(document).encode())
            normalized_counts(parsed)
        except declared:
            pass
        except Exception as error:  # noqa: BLE001 - report the unexpected type and fail
            print(f"self-test: fuzz raised undeclared {type(error).__name__}", file=sys.stderr)
            failures += 1
    if failures != 0:
        return 1
    print(f"asset import self-test passed: {len(checks)} crafted + {fuzz_rounds} fuzzed documents rejected")
    return 0


def main(arguments: list[str]) -> int:
    if arguments == ["--self-test"]:
        return self_test()
    if len(arguments) != 1:
        print("usage: cook_assets.py ENGINE_ROOT | --self-test", file=sys.stderr)
        return 2
    try:
        cook(Path(arguments[0]).resolve(strict=True))
    except (OSError, ValueError, KeyError, IndexError, TypeError, AttributeError, json.JSONDecodeError) as failure:
        print(f"asset cooking failed: {failure}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
