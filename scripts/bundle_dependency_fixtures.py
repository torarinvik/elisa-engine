"""Write the ELPK bundles the native bundle-dependency test registers.

Every bundle lives under `build/cooked/dependencies`. Each one holds a small
PNG `albedo` section, so registering it as a texture succeeds exactly when its
dependency closure is valid. A manifest names each dependency relative to the
declaring bundle's directory. Two bundles also carry the maze tile mesh, cooked
through `cook_gltf_asset.py --dependency`.
"""

from __future__ import annotations

import shutil
import struct
import zlib
from pathlib import Path

import cook_gltf_asset
from elisa_package import write_package
from packaged_maze_smoke import ENTRY_BYTES, HEADER_BYTES, section_span
from png_image import encode_png

DIRECTORY = "dependencies"
ESCAPE_LINK = "escape-link.elpk"
CHAIN_LINKS = 18
ROOT = Path(__file__).resolve().parents[1]


def replace_manifest_text(bundle: Path, old: bytes, new: bytes, *, fix_checksum: bool) -> None:
    """Swap equal-length text inside a bundle's raw manifest section."""
    data = bytearray(bundle.read_bytes())
    offset, stored = section_span(bytes(data), "manifest")
    manifest = bytes(data[offset:offset + stored])
    if len(old) != len(new) or manifest.count(old) != 1:
        raise ValueError("manifest replacement must match once and keep its length")
    data[offset:offset + stored] = manifest.replace(old, new)
    if fix_checksum:
        _magic, _version, count, _index, _size, _reserved = struct.unpack_from("<4sHHQQQ", data, 0)
        for position in range(count):
            entry = HEADER_BYTES + position * ENTRY_BYTES
            if data[entry:entry + 9] == b"manifest\0":
                struct.pack_into("<I", data, entry + 44, zlib.crc32(data[offset:offset + stored]))
    bundle.write_bytes(bytes(data))


def write_fixtures(cooked: Path, outside: Path) -> Path:
    """Write every fixture and return the escape link, which the caller removes."""
    root = cooked / DIRECTORY
    shutil.rmtree(root, ignore_errors=True)
    (root / "textures").mkdir(parents=True)
    (root / "chain").mkdir()
    albedo = {"albedo": encode_png(2, 1, bytes([200, 60, 40, 255, 40, 60, 200, 255]))}

    write_package(root / "leaf.elpk", albedo)
    write_package(root / "textures/shade.elpk", albedo)
    # "shade.elpk" means textures/shade.elpk, next to the bundle that names it.
    write_package(root / "textures/detail.elpk", albedo, ["shade.elpk"])
    write_package(root / "root.elpk", albedo, ["leaf.elpk", "textures/detail.elpk"])
    write_package(root / "missing.elpk", albedo, ["absent.elpk"])
    write_package(root / "cycle-a.elpk", albedo, ["cycle-b.elpk"])
    write_package(root / "cycle-b.elpk", albedo, ["cycle-a.elpk"])
    write_package(root / "self.elpk", albedo, ["self.elpk"])

    escaped = outside / "dependency-leaf.elpk"
    shutil.copyfile(root / "leaf.elpk", escaped)
    link = root / ESCAPE_LINK
    link.symlink_to(escaped)
    write_package(root / "escape.elpk", albedo, [ESCAPE_LINK])

    # The dependency's manifest fails its CRC-32 check. Both names it could
    # hold exist, so only the checksum can reject it.
    write_package(root / "twin-a.elpk", albedo)
    write_package(root / "twin-b.elpk", albedo)
    write_package(root / "corrupt-leaf.elpk", albedo, ["twin-a.elpk"])
    replace_manifest_text(root / "corrupt-leaf.elpk", b"twin-a", b"twin-b", fix_checksum=False)
    write_package(root / "corrupt-manifest.elpk", albedo, ["corrupt-leaf.elpk"])
    # A checksummed manifest naming an existing bundle through "..", which the
    # writer refuses to produce.
    write_package(root / "parent.elpk", albedo, ["ab/render-scene-textures.elpk"])
    replace_manifest_text(root / "parent.elpk", b"ab/render", b"../render", fix_checksum=True)

    # link-k depends on link-(k+1): link-1 has 16 transitive dependencies, link-0 has 17.
    for position in range(CHAIN_LINKS):
        following = [] if position == CHAIN_LINKS - 1 else [f"link-{position + 1}.elpk"]
        write_package(root / f"chain/link-{position}.elpk", albedo, following)

    tile = ROOT / "examples/maze/assets/maze_tile.gltf"
    for name, dependency in (("mesh.elpk", "leaf.elpk"), ("mesh-missing.elpk", "absent.elpk")):
        if cook_gltf_asset.main([str(tile), "--asset-path", "assets/maze_tile.gltf",
                "--output", str(root / name), "--dependency", dependency]) != 0:
            raise RuntimeError(f"could not cook the {name} dependency fixture")
    return link
