#!/usr/bin/env python3
"""Self-test for glTF material textures cooked into mesh slot records.

The textured panel draws three up-facing strips across the panel and a red,
emissive backdrop under the first. From -x to +x:

- cutout: an alpha-masked, double-sided strip whose 32x32 image is opaque
  green in its left half and transparent in its right, so the backdrop shows
  through there. The image is a data URI.
- painted: a lit strip sampling a 16x16 base color that is red in its left
  half and blue in its right, a flat 8x8 normal map, and a 4x4 surface image
  that also carries occlusion.
- glow: a black strip whose emissive texture samples the painted strip's base
  color image through a second texture.

Run with --write-fixture to regenerate test/fixtures/textured_panel.gltf.
"""

from __future__ import annotations

import base64
from copy import deepcopy
import json
from pathlib import Path
import struct
import sys

import cook_gltf_geometry
import cook_gltf_textures
import cook_gltf_mikktspace
from ktx2_fixtures import make_ktx2
from elisa_package import build_package_bytes, encoded_image_dimensions
from ktx2_container import (KTX2_UASTC_HDR_4X4_DFD_MODEL, KTX2_UASTC_HDR_4X4_VK_FORMAT,
    with_key_value)
from png_image import encode_png


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "test/fixtures/textured_panel.gltf"
ASSET_PATH = "test/fixtures/textured_panel.gltf"
RED, GREEN, BLUE, CLEAR_GREEN = (255, 0, 0, 255), (0, 255, 0, 255), (0, 0, 255, 255), (0, 255, 0, 0)
KHR_TEXTURE_BASISU = cook_gltf_textures.KHR_TEXTURE_BASISU


def image(size: int, pixel) -> bytes:
    """A size x size PNG whose pixel at (column, row) is pixel(column)."""
    return encode_png(size, size, bytes(channel for _ in range(size) for column in range(size)
        for channel in pixel(column)))


# Images 0-2 live in the buffer; image 3 is a data URI.
IMAGES = (
    image(16, lambda column: RED if column < 8 else BLUE),
    image(8, lambda column: (128, 128, 255, 255)),
    image(4, lambda column: (255, 255, 0, 255)),
    image(32, lambda column: GREEN if column < 16 else CLEAR_GREEN),
)
# Each strip's x range and height in panel space: cutout, painted, glow, and
# the backdrop under the cutout.
STRIPS = ((-1.0, -1 / 3, 0.0), (-1 / 3, 1 / 3, 0.0), (1 / 3, 1.0, 0.0), (-1.0, -1 / 3, -0.5))
MATERIALS = [
    {"name": "cutout", "pbrMetallicRoughness": {"baseColorTexture": {"index": 3}, "metallicFactor": 0.0},
        "alphaMode": "MASK", "alphaCutoff": 0.5, "doubleSided": True},
    {"name": "painted", "pbrMetallicRoughness": {"baseColorTexture": {"index": 0},
        "metallicRoughnessTexture": {"index": 2}}, "normalTexture": {"index": 1, "scale": 1.0},
        "occlusionTexture": {"index": 2, "strength": 1.0}},
    {"name": "glow", "pbrMetallicRoughness": {"baseColorFactor": [0.0, 0.0, 0.0, 1.0], "metallicFactor": 0.0},
        "emissiveFactor": [1.0, 1.0, 1.0], "emissiveTexture": {"index": 4}},
    {"name": "backdrop", "pbrMetallicRoughness": {"baseColorFactor": [0.0, 0.0, 0.0, 1.0],
        "metallicFactor": 0.0}, "emissiveFactor": [1.0, 0.0, 0.0]},
]
# What the cooker must record for the fixture.
SLOT_MATERIALS = (
    struct.pack("<10f2I", 1.0, 1.0, 1.0, 1.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.5, 1, 1) +
    struct.pack("<10f2I", 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 0.0, 0.0, 0.0, 0.5, 0, 2) +
    struct.pack("<10f2I", 0.0, 0.0, 0.0, 1.0, 0.0, 1.0, 1.0, 1.0, 1.0, 0.5, 0, 0) +
    struct.pack("<10f2I", 0.0, 0.0, 0.0, 1.0, 0.0, 1.0, 1.0, 0.0, 0.0, 0.5, 0, 0))
SLOT_TEXTURES = struct.pack("<20I", 4, 0, 0, 0, 0, 1, 2, 3, 0, 3, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0)
BASIS_SLOT_TEXTURES = struct.pack("<20I", 3, 0, 0, 0, 0, 4, 1, 2, 0, 2,
    0, 0, 0, 4, 0, 0, 0, 0, 0, 0)
SEPARATE_OCCLUSION_TEXTURES = struct.pack(
    "<20I", 4, 0, 0, 0, 0, 1, 2, 3, 0, 2, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0)
NO_SURFACE_TEXTURES = struct.pack(
    "<20I", 4, 0, 0, 0, 0, 1, 2, 0, 0, 3, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0)
SECTIONS = [(f"image_{index}", data) for index, data in enumerate(IMAGES)]
SUBSETS = [(0, 6, 0), (6, 6, 1), (12, 6, 2), (18, 6, 3)]


def padded(data: bytes) -> bytes:
    return data + bytes(-len(data) % 4)


def basis_ktx2(width: int = 4, height: int = 4) -> bytes:
    """Small bounded KTX2 container shape for cooker-boundary tests."""
    return make_ktx2(width, height)


def basis_hdr_ktx2() -> bytes:
    """Small UASTC HDR 4x4 KTX2 profile for image-package boundary tests."""
    data = bytearray(make_ktx2(scheme=2, levels=(bytes(16),)))
    dfd_offset = struct.unpack_from("<I", data, 48)[0]
    struct.pack_into("<I", data, 12, KTX2_UASTC_HDR_4X4_VK_FORMAT)
    data[dfd_offset + 12:dfd_offset + 17] = bytes((KTX2_UASTC_HDR_4X4_DFD_MODEL, 1, 1, 0, 3))
    data[dfd_offset + 17:dfd_offset + 20] = bytes((3, 0, 0))
    data[dfd_offset + 20] = 16
    data[dfd_offset + 31] = 0x80
    struct.pack_into("<I", data, dfd_offset + 40, 0x3F800000)
    return bytes(data)


def basis_texture(document: dict) -> None:
    image_index = len(document["images"])
    ktx2 = basis_ktx2()
    document["images"].append({"uri": "data:image/ktx2;base64," + base64.b64encode(ktx2).decode("ascii"),
        "mimeType": "image/ktx2"})
    # Keep the original PNG as each texture's fallback. The engine cooks the
    # KTX2 source and omits the fallback because its runtime supports Basis.
    for texture_index in (0, 4):
        document["textures"][texture_index]["extensions"] = {
            KHR_TEXTURE_BASISU: {"source": image_index}}
    document["extensionsUsed"] = [KHR_TEXTURE_BASISU]


def wrong_basis_mime(document: dict) -> None:
    basis_texture(document)
    data = basis_ktx2()
    document["images"][4].update(mimeType="image/png",
        uri="data:image/png;base64," + base64.b64encode(data).decode("ascii"))


def invalid_basis_payload(document: dict, offset: int, value: int) -> None:
    basis_texture(document)
    data = bytearray(basis_ktx2())
    struct.pack_into("<I", data, offset, value)
    document["images"][4]["uri"] = "data:image/ktx2;base64," + base64.b64encode(data).decode("ascii")


def core_source_uses_ktx2(document: dict) -> None:
    basis_texture(document)
    document["textures"][0].pop("extensions")
    document["textures"][0]["source"] = 4


def basis_texture_without_fallback(document: dict) -> None:
    basis_texture(document)
    document["images"] = document["images"][1:]
    for texture_index in (0, 4):
        texture = document["textures"][texture_index]
        texture.pop("source")
        texture["extensions"][KHR_TEXTURE_BASISU]["source"] = 3
    for texture_index, image_index in ((1, 0), (2, 1), (3, 2)):
        document["textures"][texture_index]["source"] = image_index
    document["extensionsRequired"] = [KHR_TEXTURE_BASISU]


def textured_panel() -> dict:
    positions, normals, uvs, indices = bytearray(), bytearray(), bytearray(), bytearray()
    for strip, (x_low, x_high, y) in enumerate(STRIPS):
        for x, z, u, v in ((x_low, -1.0, 0.0, 0.0), (x_low, 1.0, 0.0, 1.0),
                (x_high, 1.0, 1.0, 1.0), (x_high, -1.0, 1.0, 0.0)):
            positions += struct.pack("<3f", x, y, z)
            normals += struct.pack("<3f", 0.0, 1.0, 0.0)
            uvs += struct.pack("<2f", u, v)
        indices += struct.pack("<6H", *(strip * 4 + corner for corner in (0, 1, 3, 1, 2, 3)))
    blocks = [bytes(positions), bytes(normals), bytes(uvs),
        *(bytes(indices[strip * 12:strip * 12 + 12]) for strip in range(4)), *IMAGES[:3]]
    views, buffer = [], b""
    for block in blocks:
        views.append({"buffer": 0, "byteOffset": len(buffer), "byteLength": len(block)})
        buffer += padded(block)
    accessors = [
        {"bufferView": 0, "componentType": 5126, "count": 16, "type": "VEC3",
            "min": [-1.0, -0.5, -1.0], "max": [1.0, 0.0, 1.0]},
        {"bufferView": 1, "componentType": 5126, "count": 16, "type": "VEC3"},
        {"bufferView": 2, "componentType": 5126, "count": 16, "type": "VEC2"},
        *({"bufferView": 3 + strip, "componentType": 5123, "count": 6, "type": "SCALAR"} for strip in range(4)),
    ]
    attributes = {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}
    return {
        "asset": {"version": "2.0", "generator": "Elisa textured panel fixture"},
        "scene": 0,
        "scenes": [{"name": "panel", "nodes": [0]}],
        "nodes": [{"name": "panel", "mesh": 0}],
        "meshes": [{"name": "panel", "primitives": [
            {"attributes": dict(attributes), "indices": 3 + strip, "material": strip} for strip in range(4)]}],
        "materials": deepcopy(MATERIALS),
        "textures": [{"source": 0, "sampler": 0}, {"source": 1}, {"source": 2}, {"source": 3},
            {"name": "glow", "source": 0}],
        "samplers": [{"magFilter": cook_gltf_textures.LINEAR, "minFilter": cook_gltf_textures.LINEAR_MIPMAP_LINEAR,
            "wrapS": cook_gltf_textures.REPEAT, "wrapT": cook_gltf_textures.REPEAT}],
        "images": [
            *({"name": f"image_{index}", "bufferView": 7 + index, "mimeType": "image/png"} for index in range(3)),
            {"name": "cutout", "uri": "data:image/png;base64," + base64.b64encode(IMAGES[3]).decode("ascii")},
        ],
        "buffers": [{"byteLength": len(buffer),
            "uri": "data:application/octet-stream;base64," + base64.b64encode(buffer).decode("ascii")}],
        "bufferViews": views,
        "accessors": accessors,
    }


def fixture_text() -> str:
    return json.dumps(textured_panel(), indent=2) + "\n"


def fail(message: str) -> int:
    print(f"glTF cooker self-test failed: {message}", file=sys.stderr)
    return 1


def material(index: int, **fields):
    return lambda document: document["materials"][index].update(fields)


def factors(index: int, **fields):
    return lambda document: document["materials"][index]["pbrMetallicRoughness"].update(fields)


def info(slot: int, key: str, /, **fields):
    """Update one textureInfo of a material."""
    def mutate(document: dict) -> None:
        owner = document["materials"][slot]
        owner = owner["pbrMetallicRoughness"] if key in cook_gltf_textures.PBR_TEXTURES else owner
        owner[key].update(fields)
    return mutate


def entry(kind: str, position: int, /, **fields):
    return lambda document: document[kind][position].update(fields)


def drop(kind: str, index: int, key: str):
    return lambda document: document[kind][index].pop(key)


def unsampled(document: dict) -> None:
    """Strip every material texture but keep the declarations."""
    for owner in document["materials"]:
        owner.pop("alphaMode", None)
        for key in ("normalTexture", "occlusionTexture", "emissiveTexture"):
            owner.pop(key, None)
        for key in cook_gltf_textures.PBR_TEXTURES:
            owner["pbrMetallicRoughness"].pop(key, None)


def untextured(document: dict) -> None:
    """Strip every texture, leaving factor-only materials."""
    unsampled(document)
    for kind in ("textures", "samplers", "images"):
        document.pop(kind)


# The painted strip's base color also through its emissive slot: one image,
# one section, sampled twice by one slot.
def emissive_painted(document: dict) -> None:
    document["materials"][1]["emissiveTexture"] = {"index": 0}


ACCEPTED = {
    "a base-color image sampled for emission too": (emissive_painted,
        SLOT_TEXTURES[:32] + struct.pack("<I", 1) + SLOT_TEXTURES[36:], SECTIONS),
    "a data URI naming its mimeType": (entry("images", 3, mimeType="image/png"), SLOT_TEXTURES, SECTIONS),
    "a sampler with only a name": (lambda d: d["samplers"].__setitem__(0, {"name": "default"}),
        SLOT_TEXTURES, SECTIONS),
    "default textureInfo factors": (lambda d: [d["materials"][1]["normalTexture"].pop("scale"),
        d["materials"][1]["occlusionTexture"].pop("strength")], SLOT_TEXTURES, SECTIONS),
    "a non-default positive normal scale": (info(1, "normalTexture", scale=2.0), SLOT_TEXTURES, SECTIONS),
    "a negative normal scale": (info(1, "normalTexture", scale=-0.75), SLOT_TEXTURES, SECTIONS),
    "an explicit TEXCOORD_0": (info(0, "baseColorTexture", texCoord=0), SLOT_TEXTURES, SECTIONS),
    "an occlusion map from another image": (info(1, "occlusionTexture", index=1),
        SEPARATE_OCCLUSION_TEXTURES, SECTIONS),
    "an occlusion map without a surface image": (lambda d: d["materials"][1]["pbrMetallicRoughness"].pop(
        "metallicRoughnessTexture"), NO_SURFACE_TEXTURES, SECTIONS),
    "KHR_texture_basisu with a PNG fallback": (basis_texture, BASIS_SLOT_TEXTURES,
        [*SECTIONS[1:], ("image_4", basis_ktx2())]),
    "KHR_texture_basisu without a fallback": (basis_texture_without_fallback, BASIS_SLOT_TEXTURES,
        [("image_0", IMAGES[1]), ("image_1", IMAGES[2]), ("image_2", IMAGES[3]),
            ("image_3", basis_ktx2())]),
}

REJECTED = {
    "a second UV set": (info(1, "baseColorTexture", texCoord=1), "must sample TEXCOORD_0"),
    "a boolean texCoord": (info(1, "baseColorTexture", texCoord=False), "must sample TEXCOORD_0"),
    "a texture transform": (info(1, "baseColorTexture", extensions={"KHR_texture_transform": {}}),
        "baseColorTexture has unsupported properties"),
    "a scaled base-color texture": (info(1, "baseColorTexture", scale=1.0), "has unsupported properties"),
    "a string normal scale": (info(1, "normalTexture", scale="1"), "scale must be finite"),
    "a boolean normal scale": (info(1, "normalTexture", scale=True), "scale must be finite"),
    "a NaN normal scale": (info(1, "normalTexture", scale=float("nan")), "scale must be finite"),
    "an infinite normal scale": (info(1, "normalTexture", scale=float("inf")), "scale must be finite"),
    "a normal scale outside Wicked's half-float range": (info(1, "normalTexture", scale=65505.0),
        "must fit Wicked's finite half-float range"),
    "a huge integer normal scale": (info(1, "normalTexture", scale=10 ** 10000),
        "must fit Wicked's finite half-float range"),
    "an occlusion strength of 0.5": (info(1, "occlusionTexture", strength=0.5),
        "must be 1 until runtime AO strength is supported"),
    "a texture index out of range": (info(1, "baseColorTexture", index=5), "names a missing texture"),
    "a string texture index": (info(1, "baseColorTexture", index="0"), "names a missing texture"),
    "a negative texture index": (info(1, "baseColorTexture", index=-1), "names a missing texture"),
    "a textureInfo that is not an object": (factors(1, baseColorTexture=0), "has unsupported properties"),
    "a texture without a source": (drop("textures", 1, "source"), "texture names a missing image"),
    "a source out of range": (entry("textures", 1, source=4), "texture names a missing image"),
    "a negative source": (entry("textures", 1, source=-1), "texture names a missing image"),
    "a texture extension": (entry("textures", 0, extensions={"EXT_texture_webp": {}}),
        "texture has unsupported properties"),
    "a malformed Basis extension": (entry("textures", 0,
        extensions={KHR_TEXTURE_BASISU: {"source": 4, "unknown": 1}}),
        "KHR_texture_basisu must name one image source"),
    "a sampler out of range": (entry("textures", 1, sampler=1), "names a missing sampler"),
    "a nearest magnification filter": (entry("samplers", 0, magFilter=9728), "must filter trilinearly"),
    "a bilinear minification filter": (entry("samplers", 0, minFilter=9729), "must filter trilinearly"),
    "a clamped wrap": (entry("samplers", 0, wrapT=33071), "must filter trilinearly and repeat"),
    "a string filter": (entry("samplers", 0, magFilter="9729"), "must filter trilinearly"),
    "a float filter": (entry("samplers", 0, magFilter=9729.0), "must filter trilinearly"),
    "sampler extras": (entry("samplers", 0, extras={}), "sampler has unsupported properties"),
    "an unused texture": (lambda d: d["textures"].append({"source": 0}), "a texture no cooked material samples"),
    "an unused sampler": (lambda d: d["samplers"].append({}), "a sampler no cooked texture uses"),
    "an unused image": (lambda d: d["images"].append(deepcopy(d["images"][0])),
        "an image no cooked material samples"),
    "textures no material samples": (unsampled, "declares textures no cooked material samples"),
    "an image file": (entry("images", 3, uri="cutout.png"), "must be embedded"),
    "a remote URI with a base64 suffix": (entry("images", 3, uri="https://example.com/cutout.png;base64,AAAA"),
        "must be embedded"),
    "a bufferView beside a URI": (entry("images", 3, bufferView=7), "names both a bufferView and a URI"),
    "a bufferView image without a mimeType": (drop("images", 0, "mimeType"), "must be a PNG, JPEG or KTX2"),
    "a GIF image": (entry("images", 0, mimeType="image/gif"), "must be a PNG, JPEG or KTX2"),
    "a data URI contradicting its mimeType": (entry("images", 3, mimeType="image/jpeg"), "contradicts its data URI"),
    "PNG bytes labeled JPEG": (entry("images", 1, mimeType="image/jpeg"), "do not match its mimeType"),
    "a data URI that is not base64": (entry("images", 3, uri="data:image/png;base64,!!"), "not valid base64"),
    "a data URI without an image": (entry("images", 3, uri="data:image/png;base64,AAAA"),
        "do not match its mimeType"),
    "a truncated PNG": (lambda d: d["images"][3].update(uri="data:image/png;base64," +
        base64.b64encode(IMAGES[3][:20]).decode("ascii")), "PNG"),
    "a strided image view": (entry("bufferViews", 7, byteStride=4), "packed view of the embedded buffer"),
    "an image view in another buffer": (entry("bufferViews", 8, buffer=1), "packed view of the embedded buffer"),
    "an image view past the buffer": (entry("bufferViews", 9, byteLength=1 << 20), "outside the buffer"),
    "an image view out of range": (entry("images", 0, bufferView=11), "names a missing bufferView"),
    "image extras": (entry("images", 2, extras={}), "image has unsupported properties"),
    "textures that are not a list": (lambda d: d.update(textures={}), "names a missing texture"),
    "a textured strip without UVs": (lambda d: d["meshes"][0]["primitives"][1]["attributes"].pop("TEXCOORD_0"),
        "a primitive with a textured material needs TEXCOORD_0"),
    "a malformed tangent stream": (lambda d: d["meshes"][0]["primitives"][0]["attributes"].update(TANGENT=1),
        "tangents must be float32 VEC4 values"),
    "a mask without a base-color image": (lambda d: d["materials"][0]["pbrMetallicRoughness"].pop(
        "baseColorTexture"), "alpha-mask materials need a base-color texture"),
    "a Basis extension missing from extensionsUsed":
        (lambda d: [basis_texture(d), d.pop("extensionsUsed")], "must be listed in extensionsUsed"),
    "a Basis extension without a fallback but not required":
        (lambda d: [basis_texture_without_fallback(d), d.pop("extensionsRequired")],
            "without a fallback must be listed in extensionsRequired"),
    "a non-KTX2 Basis source": (wrong_basis_mime, "do not match its mimeType"),
    "a KTX2 source on the core texture field":
        (core_source_uses_ktx2, "must be selected by KHR_texture_basisu"),
    "a non-universal KTX2 pixel format":
        (lambda d: invalid_basis_payload(d, 12, 37), "Basis Universal KTX2 payload"),
    "a non-Basis KTX2 supercompression scheme":
        (lambda d: invalid_basis_payload(d, 44, 3), "Basis Universal KTX2 payload"),
    "Basis KTX2 dimensions not divisible by four":
        (lambda d: [basis_texture(d), d["images"][4].update(uri="data:image/ktx2;base64," +
            base64.b64encode(basis_ktx2(6, 4)).decode("ascii"))], "multiples of 4"),
}


def rejected_reason(document: dict, buffer: bytes) -> str | None:
    try:
        cook_gltf_geometry.normalized_geometry(document, buffer)
    except ValueError as error:
        return str(error)
    return None


def material_texture_self_test(temporary: Path, cook_main) -> int:
    """`cook_main` is the asset cooker's command line."""
    if not SOURCE.is_file() or SOURCE.read_text(encoding="utf-8") != fixture_text():
        return fail("test/fixtures/textured_panel.gltf is not what --write-fixture writes")
    try:
        hdr_dimensions = encoded_image_dimensions(basis_hdr_ktx2())
    except ValueError as error:
        return fail(f"the bounded image cooker rejected UASTC HDR KTX2: {error}")
    if hdr_dimensions != (4, 4):
        return fail("UASTC HDR KTX2 did not preserve its validated dimensions")
    try:
        swizzled = with_key_value(basis_ktx2(), "KTXswizzle", b"ra01\0")
        replaced = with_key_value(swizzled, "KTXswizzle", b"rgba\0")
    except ValueError as error:
        return fail(f"KTX2 swizzle metadata update failed: {error}")
    if (encoded_image_dimensions(swizzled) != (4, 4) or
            encoded_image_dimensions(replaced) != (4, 4) or swizzled == replaced):
        return fail("KTX2 swizzle metadata did not round-trip through the bounded parser")
    malformed_hdr = bytearray(basis_hdr_ktx2())
    malformed_hdr[struct.unpack_from("<I", malformed_hdr, 48)[0] + 12] = 166
    try:
        encoded_image_dimensions(bytes(malformed_hdr))
    except ValueError:
        pass
    else:
        return fail("the bounded image cooker accepted an HDR vkFormat with an LDR DFD")
    hdr_dfd = struct.unpack_from("<I", basis_hdr_ktx2(), 48)[0]
    for label, relative_offset, value in (
            ("sample qualifier", 31, 0), ("sample range", 40, 0xFFFFFFFF),
            ("transfer function", 14, 2)):
        malformed_profile = bytearray(basis_hdr_ktx2())
        if relative_offset == 40:
            struct.pack_into("<I", malformed_profile, hdr_dfd + relative_offset, value)
        else:
            malformed_profile[hdr_dfd + relative_offset] = value
        try:
            encoded_image_dimensions(bytes(malformed_profile))
        except ValueError:
            continue
        return fail(f"the bounded image cooker accepted an HDR DFD with an invalid {label}")
    unsupported_hdr = bytearray(basis_ktx2())
    unsupported_hdr[struct.unpack_from("<I", unsupported_hdr, 48)[0] + 12] = 168
    try:
        encoded_image_dimensions(bytes(unsupported_hdr))
    except ValueError:
        pass
    else:
        return fail("the bounded image cooker accepted an unimplemented UASTC HDR 6x6 profile")
    document = textured_panel()
    buffer = base64.b64decode(document["buffers"][0]["uri"].split(",", 1)[1])
    geometry = cook_gltf_geometry.normalized_geometry(document, buffer)
    if (geometry["slot_materials"] != SLOT_MATERIALS or geometry["slot_textures"] != SLOT_TEXTURES or
            geometry["slot_normal_scales"] or geometry["images"] != SECTIONS or geometry["vertex_count"] != 16 or
            geometry["subsets"] != SUBSETS or len(geometry["tangents"]) != 16 * 16):
        return fail("the textured panel cooked the wrong slot records, images or subsets")
    tangent_values = list(struct.iter_unpack("<4f", geometry["tangents"]))
    if any(abs(tangent[0] - 1.0) > 1.0e-5 or abs(tangent[1]) > 1.0e-5 or
            abs(tangent[2]) > 1.0e-5 or abs(abs(tangent[3]) - 1.0) > 1.0e-5
            for tangent in tangent_values):
        return fail("the textured panel did not receive deterministic tangent frames")
    names = b"".join(struct.pack("<I", 7) + name.encode("ascii") for name, _ in SECTIONS)
    lines = cook_gltf_geometry.subset_lines(geometry)
    expected_lines = ["texture_count=4", "texture_names_b64=" + base64.b64encode(names).decode("ascii"),
        "slot_texture_stride=20", "slot_textures_b64=" + base64.b64encode(SLOT_TEXTURES).decode("ascii")]
    if lines[-4:] != expected_lines:
        return fail("the textured panel's slot texture lines are wrong")
    scaled_document = deepcopy(document)
    info(1, "normalTexture", scale=2.0)(scaled_document)
    scaled_geometry = cook_gltf_geometry.normalized_geometry(scaled_document, buffer)
    if scaled_geometry["slot_normal_scales"] != struct.pack("<4f", 1.0, 2.0, 1.0, 1.0):
        return fail("the glTF cooker did not preserve non-default normal scale per material slot")
    info(1, "normalTexture", scale=-0.75)(scaled_document)
    negative_scale_geometry = cook_gltf_geometry.normalized_geometry(scaled_document, buffer)
    if negative_scale_geometry["slot_normal_scales"] != struct.pack("<4f", 1.0, -0.75, 1.0, 1.0):
        return fail("the glTF cooker did not preserve a negative normal scale")

    # The package lists the images, a bundle carries them, and only a bundle
    # may: a loose package would name sections nothing holds.
    bundles = []
    for label in ("first", "second"):
        bundle = temporary / f"textured-{label}.elpk"
        if cook_main([str(SOURCE), "--asset-path", ASSET_PATH, "--output", str(bundle)]) != 0:
            return fail("the textured panel did not cook into a bundle")
        bundles.append(bundle.read_bytes())
    package, _ = cook_gltf_geometry.cook_geometry_package(SOURCE, ASSET_PATH, temporary / "textured.pkg",
        allow_textures=True)
    if bundles[0] != bundles[1] or bundles[0] != build_package_bytes({"mesh": package.read_bytes(), **dict(SECTIONS)}):
        return fail("the textured bundle is unstable or lacks the mesh and image sections")
    try:
        cook_gltf_geometry.cook_geometry_package(SOURCE, ASSET_PATH, temporary / "loose.pkg")
        return fail("a textured source cooked into a loose package")
    except ValueError as error:
        if "need an .elpk bundle output" not in str(error):
            return fail(f"a textured loose package failed for another reason: {error}")
    for arguments in (["--output", str(temporary / "loose.pkg")],
            ["--output", str(temporary / "clash.elpk"), "--texture", f"image_2={temporary / 'image.png'}"]):
        (temporary / "image.png").write_bytes(IMAGES[2])
        if cook_main([str(SOURCE), "--asset-path", ASSET_PATH, *arguments]) == 0:
            return fail(f"the asset cooker accepted {arguments[-1]}")

    basis_document = deepcopy(document)
    basis_texture(basis_document)
    basis_source = temporary / "textured-basis.gltf"
    basis_source.write_text(json.dumps(basis_document), encoding="utf-8")
    basis_asset_path = "test/fixtures/textured_basis.gltf"
    basis_bundle = temporary / "textured-basis.elpk"
    if cook_main([str(basis_source), "--asset-path", basis_asset_path,
            "--output", str(basis_bundle)]) != 0:
        return fail("KHR_texture_basisu did not cook into the mesh bundle")
    basis_geometry, basis_result = cook_gltf_geometry.cook_geometry_package(basis_source, basis_asset_path,
        temporary / "textured-basis.pkg", allow_textures=True)
    expected_basis_bundle = build_package_bytes({"mesh": basis_geometry.read_bytes(), **dict(basis_result["images"])})
    if basis_bundle.read_bytes() != expected_basis_bundle:
        return fail("the Basis texture bundle omitted or changed a cooked KTX2 image section")

    untextured_document = deepcopy(document)
    untextured(untextured_document)
    plain = cook_gltf_geometry.normalized_geometry(untextured_document, buffer)
    if plain["slot_textures"] != b"" or plain["images"] or any(line.startswith("texture") or
            line.startswith("slot_texture") for line in cook_gltf_geometry.subset_lines(plain)):
        return fail("an untextured panel cooked texture records")

    mirrored_document = deepcopy(untextured_document)
    mirrored_buffer = bytearray(buffer)
    uv_view = mirrored_document["bufferViews"][2]
    uv_offset = uv_view.get("byteOffset", 0)
    panel_uvs = list(struct.unpack_from("<32f", mirrored_buffer, uv_offset))
    panel_uvs[4:6] = (-1.0, 1.0)
    struct.pack_into("<32f", mirrored_buffer, uv_offset, *panel_uvs)
    morph_values = [float(index + 1) for index in range(16) for _ in range(3)]
    if len(mirrored_buffer) % 4:
        mirrored_buffer.extend(bytes(-len(mirrored_buffer) % 4))
    morph_view = len(mirrored_document["bufferViews"])
    morph_offset = len(mirrored_buffer)
    morph_payload = struct.pack("<48f", *morph_values)
    mirrored_buffer.extend(morph_payload)
    mirrored_document["bufferViews"].append({"buffer": 0, "byteOffset": morph_offset,
        "byteLength": len(morph_payload)})
    morph_accessor = len(mirrored_document["accessors"])
    mirrored_document["accessors"].append({"bufferView": morph_view, "componentType": 5126,
        "count": 16, "type": "VEC3"})
    for primitive in mirrored_document["meshes"][0]["primitives"]:
        primitive["targets"] = [{"POSITION": morph_accessor}]
    all_indices = []
    for primitive in mirrored_document["meshes"][0]["primitives"]:
        all_indices.extend(cook_gltf_geometry.read_indices(
            mirrored_document, mirrored_buffer, primitive, 16))

    def view_bytes(index: int) -> bytes:
        view = mirrored_document["bufferViews"][index]
        start = view.get("byteOffset", 0)
        return bytes(mirrored_buffer[start:start + view["byteLength"]])

    try:
        expected_mikk = cook_gltf_mikktspace.generate(
            view_bytes(0), view_bytes(1), view_bytes(2), 16, all_indices)
    except (ValueError, struct.error) as error:
        return fail(f"MikkTSpace reference fixture failed: {error}")
    try:
        mirrored_geometry = cook_gltf_geometry.normalized_geometry(
            mirrored_document, bytes(mirrored_buffer))
    except ValueError as error:
        return fail(f"MikkTSpace rejected the mirrored glTF panel: {error}")
    if (mirrored_geometry["vertex_count"] != len(expected_mikk["source_vertices"]) or
            mirrored_geometry["positions"] != expected_mikk["positions"] or
            mirrored_geometry["normals"] != expected_mikk["normals"] or
            mirrored_geometry["uvs"] != expected_mikk["uvs"] or
            mirrored_geometry["tangents"] != expected_mikk["tangents"]):
        return fail("the glTF cooker did not preserve MikkTSpace seam-split vertex streams")
    cooked_indices = list(struct.unpack(
        f"<{mirrored_geometry['index_count']}I", mirrored_geometry["indices"]))
    cooked_tangents = list(struct.iter_unpack("<4f", mirrored_geometry["tangents"]))
    first_subset = mirrored_geometry["subsets"][0]
    triangle_signs = []
    for cursor in range(first_subset[0], first_subset[0] + first_subset[1], 3):
        triangle = cooked_indices[cursor:cursor + 3]
        signs = {cooked_tangents[index][3] for index in triangle}
        if len(signs) != 1:
            return fail("a mirrored glTF triangle received inconsistent tangent handedness")
        triangle_signs.append(next(iter(signs)))
    morph_output = mirrored_geometry["morph_targets"][0]["positions"]
    morph_output_values = list(struct.iter_unpack("<3f", morph_output))
    if (len(triangle_signs) != 2 or triangle_signs[0] == triangle_signs[1] or
            len(morph_output_values) != len(expected_mikk["source_vertices"]) or
            any(morph_output_values[index][0] != source + 1.0
                for index, source in enumerate(expected_mikk["source_vertices"]))):
        return fail("the glTF cooker lost mirrored seam handedness or morph deltas")
    for label, (mutate, records, sections) in ACCEPTED.items():
        variant = deepcopy(document)
        mutate(variant)
        try:
            cooked = cook_gltf_geometry.normalized_geometry(variant, buffer)
        except ValueError as error:
            return fail(f"rejected {label}: {error}")
        if cooked["slot_textures"] != records or cooked["images"] != sections:
            return fail(f"cooked {label} wrong")
    for label, (mutate, reason) in REJECTED.items():
        variant = deepcopy(document)
        mutate(variant)
        error = rejected_reason(variant, buffer)
        if error is None:
            return fail(f"accepted {label}")
        if reason not in error:
            return fail(f"rejected {label} for another reason: {error}")
    return 0


if __name__ == "__main__":
    if sys.argv[1:] != ["--write-fixture"]:
        raise SystemExit("usage: gltf_texture_self_test.py --write-fixture")
    SOURCE.write_text(fixture_text(), encoding="utf-8")
