"""Resolve static glTF material textures into runtime slots and bundle images.

The render scene samples every material texture with one trilinear, repeating
sampler through the first UV set. Metallic and roughness remain packed in the
surface image, while occlusion may use that image or its own image and native
Wicked material slot.
"""

from __future__ import annotations

import base64
import binascii
import struct

from elisa_package import KTX2_IDENTIFIER, MAX_SECTION_BYTES, encoded_image_dimensions

# Runtime texture slots, in the order a slot texture record lists them.
SLOT_TEXTURES = ("baseColorTexture", "normalTexture", "metallicRoughnessTexture", "emissiveTexture",
    "occlusionTexture")
PBR_TEXTURES = {"baseColorTexture", "metallicRoughnessTexture"}
SURFACE = 2
SLOT_TEXTURE_STRIDE = 20
LINEAR = 9729
LINEAR_MIPMAP_LINEAR = 9987
REPEAT = 10497
KHR_TEXTURE_BASISU = "KHR_texture_basisu"
IMAGE_SIGNATURES = {"image/png": b"\x89PNG\r\n\x1a\n", "image/jpeg": b"\xff\xd8\xff",
    "image/ktx2": KTX2_IDENTIFIER}


def check_sampler(document: dict, reference) -> None:
    samplers = document.get("samplers", [])
    if type(reference) is not int or not isinstance(samplers, list) or not 0 <= reference < len(samplers):
        raise ValueError("glTF texture names a missing sampler")
    sampler = samplers[reference]
    if not isinstance(sampler, dict) or set(sampler) - {"magFilter", "minFilter", "wrapS", "wrapT", "name"}:
        raise ValueError("glTF sampler has unsupported properties")
    expected = {"magFilter": LINEAR, "minFilter": LINEAR_MIPMAP_LINEAR, "wrapS": REPEAT, "wrapT": REPEAT}
    if any(type(sampler.get(key, value)) is not int or sampler.get(key, value) != value
            for key, value in expected.items()):
        raise ValueError("glTF sampler must filter trilinearly and repeat, as the runtime samples")


def texture_image(document: dict, info, label: str, factor: str | None = None) -> int:
    """Return the image a material textureInfo samples. `factor` names the
    info's scale or strength, which must be 1."""
    allowed = {"index", "texCoord"} | ({factor} if factor else set())
    if not isinstance(info, dict) or set(info) - allowed:
        raise ValueError(f"material {label} has unsupported properties")
    coordinate = info.get("texCoord", 0)
    if type(coordinate) is not int or coordinate != 0:
        raise ValueError(f"material {label} must sample TEXCOORD_0")
    if factor and factor in info and (type(info[factor]) not in (int, float) or info[factor] != 1):
        raise ValueError(f"material {label} {factor} must be 1")
    textures = document.get("textures", [])
    index = info.get("index")
    if type(index) is not int or not isinstance(textures, list) or not 0 <= index < len(textures):
        raise ValueError(f"material {label} names a missing texture")
    texture = textures[index]
    if not isinstance(texture, dict) or set(texture) - {"source", "sampler", "name", "extensions"}:
        raise ValueError("glTF texture has unsupported properties")
    images = document.get("images", [])
    extensions = texture.get("extensions", {})
    if not isinstance(extensions, dict) or set(extensions) - {KHR_TEXTURE_BASISU}:
        raise ValueError("glTF texture has unsupported properties")
    has_basis = KHR_TEXTURE_BASISU in extensions
    basis = extensions.get(KHR_TEXTURE_BASISU)
    if has_basis and (not isinstance(basis, dict) or set(basis) != {"source"}):
        raise ValueError("KHR_texture_basisu must name one image source")
    source = basis.get("source") if has_basis else texture.get("source")
    fallback = texture.get("source")
    if has_basis and fallback is not None and (
            type(fallback) is not int or not isinstance(images, list) or not 0 <= fallback < len(images)):
        raise ValueError("texture fallback names a missing image")
    if type(source) is not int or not isinstance(images, list) or not 0 <= source < len(images):
        raise ValueError("glTF texture names a missing image")
    if "sampler" in texture:
        check_sampler(document, texture["sampler"])
    return source


def material_images(document: dict, material: dict, pbr: dict) -> tuple[list, bool]:
    """Return each runtime texture slot's image, or None, and whether
    occlusion is enabled."""
    images = []
    for key in SLOT_TEXTURES[:4]:
        info = (pbr if key in PBR_TEXTURES else material).get(key)
        images.append(None if info is None else
            texture_image(document, info, key, "scale" if key == "normalTexture" else None))
    if "occlusionTexture" not in material:
        images.append(None)
        return images, False
    images.append(texture_image(document, material["occlusionTexture"], "occlusionTexture", "strength"))
    return images, True


def view_bytes(document: dict, buffer: bytes, reference) -> bytes:
    views = document.get("bufferViews", [])
    if type(reference) is not int or not isinstance(views, list) or not 0 <= reference < len(views):
        raise ValueError("glTF image names a missing bufferView")
    view = views[reference]
    if not isinstance(view, dict) or type(view.get("buffer")) is not int or view["buffer"] != 0 or \
            "byteStride" in view:
        raise ValueError("glTF image bufferView must be a packed view of the embedded buffer")
    start = view.get("byteOffset", 0)
    length = view.get("byteLength")
    if type(start) is not int or type(length) is not int or start < 0 or length <= 0 or \
            start + length > len(buffer):
        raise ValueError("glTF image bufferView lies outside the buffer")
    return buffer[start:start + length]


def image_bytes(document: dict, buffer: bytes, index: int) -> bytes:
    """Return one embedded PNG, JPEG or KTX2 image, from a bufferView or data URI."""
    image = document["images"][index]
    if not isinstance(image, dict) or set(image) - {"uri", "mimeType", "bufferView", "name"}:
        raise ValueError("glTF image has unsupported properties")
    if "bufferView" in image:
        if "uri" in image:
            raise ValueError("glTF image names both a bufferView and a URI")
        mime = image.get("mimeType")
        data = view_bytes(document, buffer, image["bufferView"])
    else:
        uri = image.get("uri")
        if not isinstance(uri, str) or not uri.startswith("data:") or ";base64," not in uri:
            raise ValueError("glTF image must be embedded in a bufferView or a base64 data URI")
        header, encoded = uri.split(";base64,", 1)
        mime = header[len("data:"):]
        if image.get("mimeType", mime) != mime:
            raise ValueError("glTF image mimeType contradicts its data URI")
        if len(encoded) > MAX_SECTION_BYTES * 2:
            raise ValueError("glTF image exceeds the section bound")
        try:
            data = base64.b64decode(encoded, validate=True)
        except binascii.Error as failure:
            raise ValueError("glTF image data URI is not valid base64") from failure
    signature = IMAGE_SIGNATURES.get(mime) if isinstance(mime, str) else None
    if signature is None:
        raise ValueError("glTF image must be a PNG, JPEG or KTX2")
    if not data.startswith(signature):
        raise ValueError("glTF image bytes do not match its mimeType")
    width, height = encoded_image_dimensions(data)
    if mime == "image/ktx2":
        if width % 4 or height % 4:
            raise ValueError("KHR_texture_basisu dimensions must be multiples of 4")
    return data


def section_name(image: int) -> str:
    return f"image_{image}"


def texture_infos(material: dict) -> list:
    """Every textureInfo of an already validated material."""
    pbr = material.get("pbrMetallicRoughness", {})
    infos = [pbr.get(key) for key in ("baseColorTexture", "metallicRoughnessTexture")]
    infos += [material.get(key) for key in ("normalTexture", "occlusionTexture", "emissiveTexture")]
    return [info for info in infos if info is not None]


def cooked_textures(document: dict, buffer: bytes, slot_images: list) -> tuple[bytes, list]:
    """Pack each slot's five image references, 0 for none or one more than
    the image's position among the sampled images, and return the records
    with each sampled image's (section name, bytes). Every texture and sampler
    must be used. Unused image declarations are accepted only as core glTF
    fallback sources for KHR_texture_basisu textures."""
    declared = {name: document.get(name, []) for name in ("textures", "samplers", "images")}
    if any(not isinstance(entries, list) for entries in declared.values()):
        raise ValueError("glTF textures, samplers and images must be lists")
    sampled = sorted({image for images in slot_images for image in images if image is not None})
    if not sampled:
        if any(declared.values()):
            raise ValueError("glTF declares textures no cooked material samples")
        return b"", []
    textures = {info["index"] for material in document["materials"] for info in texture_infos(material)}
    samplers = {declared["textures"][texture]["sampler"] for texture in textures
        if "sampler" in declared["textures"][texture]}
    if len(textures) != len(declared["textures"]):
        raise ValueError("glTF declares a texture no cooked material samples")
    if len(samplers) != len(declared["samplers"]):
        raise ValueError("glTF declares a sampler no cooked texture uses")
    fallback_images = set()
    basis_sources = set()
    core_sources = set()
    basis_without_fallback = False
    for texture in declared["textures"]:
        if not isinstance(texture, dict):
            continue
        extensions = texture.get("extensions", {})
        basis = extensions.get(KHR_TEXTURE_BASISU) if isinstance(extensions, dict) else None
        if isinstance(basis, dict):
            basis_sources.add(basis["source"])
            if "source" in texture:
                fallback_images.add(texture["source"])
            else:
                basis_without_fallback = True
        elif "source" in texture:
            core_sources.add(texture["source"])
    if set(sampled) | fallback_images != set(range(len(declared["images"]))):
        raise ValueError("glTF declares an image no cooked material samples or uses as a Basis fallback")
    if basis_sources:
        extensions_used = document.get("extensionsUsed", [])
        if not isinstance(extensions_used, list) or KHR_TEXTURE_BASISU not in extensions_used:
            raise ValueError("KHR_texture_basisu must be listed in extensionsUsed")
        extensions_required = document.get("extensionsRequired", [])
        if not isinstance(extensions_required, list):
            raise ValueError("glTF extensionsRequired must be a list")
        if basis_without_fallback and KHR_TEXTURE_BASISU not in extensions_required:
            raise ValueError("KHR_texture_basisu without a fallback must be listed in extensionsRequired")
    for fallback in fallback_images:
        if image_bytes(document, buffer, fallback).startswith(KTX2_IDENTIFIER):
            raise ValueError("KHR_texture_basisu fallback image must be PNG or JPEG")
    for source in core_sources:
        if image_bytes(document, buffer, source).startswith(KTX2_IDENTIFIER):
            raise ValueError("KTX2 glTF images must be selected by KHR_texture_basisu")
    reference = {image: position + 1 for position, image in enumerate(sampled)}
    records = b"".join(struct.pack("<5I", *(0 if image is None else reference[image] for image in images))
        for images in slot_images)
    encoded_images = []
    for image in sampled:
        data = image_bytes(document, buffer, image)
        if data.startswith(KTX2_IDENTIFIER) != (image in basis_sources):
            raise ValueError("KTX2 glTF images must be selected by KHR_texture_basisu")
        encoded_images.append((section_name(image), data))
    return records, encoded_images


def texture_lines(records: bytes, images: list) -> list[str]:
    """Slot texture records for the cooked mesh package."""
    if not images:
        return []
    names = b"".join(struct.pack("<I", len(name)) + name.encode("ascii") for name, _ in images)
    return [
        f"texture_count={len(images)}",
        "texture_names_b64=" + base64.b64encode(names).decode("ascii"),
        f"slot_texture_stride={SLOT_TEXTURE_STRIDE}",
        "slot_textures_b64=" + base64.b64encode(records).decode("ascii"),
    ]
