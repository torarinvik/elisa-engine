"""Cooked-asset probes for the validation record.

These gates read what the asset pipeline wrote -- texture packages, the
catalogue database, the cooker's own self-test -- and they are the part of the
record that grows with every new asset format. They live beside the recorder
rather than inside it so neither file drifts past the 600-line source rule the
recorder itself enforces.
"""

import base64
import hashlib
import sqlite3
import subprocess
import sys
from pathlib import Path


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def cooked_texture(root: Path) -> dict:
    # The first texture the pipeline cooks is gated too: a versioned RGBA
    # package whose declared size must match its pixel payload.
    path = root / "build/cooked/maze_tile_tex.rgba"
    if not path.is_file():
        raise ValueError("cooked texture package is missing")
    values = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            values[key.strip()] = value.strip()
    if values.get("format") != "elisa-texture-v1":
        raise ValueError(f"cooked texture format drift: {values.get('format')!r}")
    try:
        width = int(values.get("width", ""))
        height = int(values.get("height", ""))
        channels = int(values.get("channels", ""))
    except ValueError:
        raise ValueError("cooked texture dimensions are not integers")
    pixels = base64.b64decode(values.get("pixels_b64", ""))
    if width != 4 or height != 4 or channels != 4 or len(pixels) != width * height * channels:
        raise ValueError(f"cooked texture size mismatch: {width}x{height}x{channels} pixels={len(pixels)}")
    return {"sha256": sha256_file(path), "width": width, "height": height, "pixel_bytes": len(pixels)}


def cooked_texture_packed(root: Path) -> dict:
    # The 16-bit packed texture companion: declared bytes per pixel must match
    # the payload, so a mis-sized packing fails rather than loading silently.
    path = root / "build/cooked/maze_tile_tex16.rgba"
    if not path.is_file():
        raise ValueError("cooked packed texture is missing")
    values = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            values[key.strip()] = value.strip()
    if values.get("format") != "elisa-texture-v1" or values.get("packing") != "rgb565":
        raise ValueError(f"cooked packed texture format drift: {values.get('format')!r}/{values.get('packing')!r}")
    width = int(values.get("width", ""))
    height = int(values.get("height", ""))
    bytes_per_pixel = int(values.get("bytes_per_pixel", ""))
    pixels = base64.b64decode(values.get("pixels_b64", ""))
    if width != 4 or height != 4 or bytes_per_pixel != 2 or len(pixels) != width * height * bytes_per_pixel:
        raise ValueError(f"cooked packed texture size mismatch: {width}x{height}x{bytes_per_pixel} pixels={len(pixels)}")
    return {"sha256": sha256_file(path), "width": width, "height": height, "bytes_per_pixel": bytes_per_pixel}


def cooked_texture_bc1(root: Path) -> dict:
    # Block-compressed companion: a 4x4 BC1 texture is one 8-byte block, so the
    # payload must be exactly that.
    path = root / "build/cooked/maze_tile_tex_bc1.rgba"
    if not path.is_file():
        raise ValueError("cooked BC1 texture is missing")
    values = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            values[key.strip()] = value.strip()
    if values.get("format") != "elisa-texture-v1" or values.get("packing") != "bc1":
        raise ValueError(f"cooked BC1 texture format drift: {values.get('format')!r}/{values.get('packing')!r}")
    width = int(values.get("width", ""))
    height = int(values.get("height", ""))
    block_bytes = int(values.get("block_bytes", ""))
    pixels = base64.b64decode(values.get("pixels_b64", ""))
    if width != 4 or height != 4 or block_bytes != 8 or len(pixels) != 8:
        raise ValueError(f"cooked BC1 texture size mismatch: {width}x{height} block={block_bytes} pixels={len(pixels)}")
    return {"sha256": sha256_file(path), "width": width, "height": height, "block_bytes": block_bytes}


def asset_catalogue_database(root: Path) -> dict:
    # The toolkit writes a persistent SQLite catalogue during cooking. This
    # gates that the row the toolkit recorded agrees with the shared fixture
    # rather than trusting the database to exist.
    database = root / "build/catalogue.db"
    if not database.is_file():
        raise ValueError("asset catalogue database is missing")
    connection = sqlite3.connect(database)
    try:
        rows = connection.execute("SELECT source, sha256, triangles, positions, source_id, settings_hash FROM assets").fetchall()
        cache_rows = connection.execute("SELECT COUNT(*), COUNT(DISTINCT cache_key) FROM cook_cache WHERE status = 'ready'").fetchone()
        dependency_rows = connection.execute("SELECT COUNT(*) FROM dependencies").fetchone()[0]
        schema = connection.execute("SELECT value FROM catalogue_meta WHERE key = 'schema'").fetchone()
    finally:
        connection.close()
    if len(rows) != 1 or cache_rows != (1, 1) or dependency_rows < 1 or schema != ("2",):
        raise ValueError(f"asset catalogue expected one row, found {len(rows)}")
    manifest_values = {}
    for line in (root / "backends/scene_manifest.txt").read_text(encoding="utf-8").splitlines():
        text = line.strip()
        if not text or text.startswith("#") or "=" not in text:
            continue
        key, value = text.split("=", 1)
        manifest_values[key.strip()] = value.strip()
    expected_triangles = int(manifest_values.get("mesh_triangles", "0"))
    if rows[0][2] != expected_triangles or not rows[0][4] or not rows[0][5]:
        raise ValueError(
            f"asset catalogue triangles {rows[0][2]} disagree with the fixture's {expected_triangles}")
    return {"sha256": sha256_file(database), "assets": len(rows),
            "triangles": rows[0][2], "positions": rows[0][3], "cache_rows": cache_rows[0]}


def asset_import_self_test(root: Path) -> dict:
    # Malformed-asset and oversized-count handling is a gate, not a manual
    # step: the cooker's self-test must reject every crafted bad document.
    script = root / "scripts/cook_assets.py"
    result = subprocess.run(
        [sys.executable, str(script), "--self-test"],
        capture_output=True, text=True, check=False,
    )
    if result.returncode != 0:
        raise ValueError(f"asset import self-test failed: {result.stderr.strip() or result.stdout.strip()}")
    return {"sha256": sha256_file(script), "summary": result.stdout.strip()}
