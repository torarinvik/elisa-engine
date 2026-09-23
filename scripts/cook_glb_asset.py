#!/usr/bin/env python3
"""Cook a static or skinned GLB mesh through Blender and Elisa's FBX cooker.

GLB supplies geometry, optional skinning and embedded base-color images. Image
nodes are removed from the intermediate FBX because this cooker preserves
geometry and scalar materials; an explicitly requested base-color image is
extracted separately. An optional FBX animation source can provide clips for a
GLB rig with matching joint names. Static meshes may be simplified to a
bounded triangle count.
"""

from __future__ import annotations

import argparse
import base64
import binascii
import cook_image_asset
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import struct
import subprocess
import sys
import tempfile
import tempfile
from urllib.parse import unquote, urlsplit


ROOT = Path(__file__).resolve().parents[1]
MAX_GLTF_BYTES = 512 * 1024 * 1024
GLB_JSON = 0x4E4F534A
GLB_BIN = 0x004E4942


class GlbError(ValueError):
    """A GLB is malformed or uses an unsupported image representation."""


def read_glb_document(source: Path) -> tuple[dict[str, object], bytes]:
    size = source.stat().st_size
    if size < 20 or size > MAX_GLTF_BYTES:
        raise GlbError("GLB file is empty, truncated, or exceeds the 512 MiB source limit")
    data = source.read_bytes()
    magic, version, declared_length = struct.unpack_from("<4sII", data)
    if magic != b"glTF" or version != 2 or declared_length != len(data):
        raise GlbError("source is not a complete glTF 2.0 binary (GLB) file")

    document: dict[str, object] | None = None
    binary = b""
    offset = 12
    chunk_index = 0
    while offset < len(data):
        if len(data) - offset < 8:
            raise GlbError("GLB has a truncated chunk header")
        chunk_length, chunk_type = struct.unpack_from("<II", data, offset)
        offset += 8
        end = offset + chunk_length
        if chunk_length % 4 or end < offset or end > len(data):
            raise GlbError("GLB contains an invalid chunk length")
        chunk = data[offset:end]
        if chunk_index == 0:
            if chunk_type != GLB_JSON:
                raise GlbError("GLB must start with its JSON chunk")
            try:
                parsed = json.loads(chunk.rstrip(b"\0 \t\r\n").decode("utf-8"))
            except (UnicodeError, json.JSONDecodeError) as error:
                raise GlbError(f"GLB JSON is invalid: {error}") from error
            if not isinstance(parsed, dict):
                raise GlbError("GLB JSON root must be an object")
            document = parsed
        elif chunk_type == GLB_BIN:
            if binary:
                raise GlbError("GLB contains multiple binary chunks")
            binary = chunk
        elif chunk_type & 0x80000000 == 0:
            raise GlbError(f"GLB contains unsupported required chunk type 0x{chunk_type:08x}")
        offset = end
        chunk_index += 1
    if document is None:
        raise GlbError("GLB is missing its JSON document")
    buffers = document.get("buffers", [])
    if not isinstance(buffers, list) or len(buffers) > 1:
        raise GlbError("GLB must contain at most one embedded buffer")
    if buffers:
        buffer = buffers[0]
        if not isinstance(buffer, dict) or "uri" in buffer:
            raise GlbError("GLB's primary buffer must be embedded in its BIN chunk")
        byte_length = buffer.get("byteLength")
        if not isinstance(byte_length, int) or byte_length < 0 or byte_length > len(binary):
            raise GlbError("GLB buffer length exceeds its binary chunk")
    return document, binary


def base_color_image(source: Path, document: dict[str, object], binary: bytes) -> tuple[bytes, str]:
    """Find and read the first material's base-color image."""
    materials = document.get("materials", [])
    textures = document.get("textures", [])
    images = document.get("images", [])
    if not isinstance(materials, list) or not materials or not isinstance(textures, list) or not isinstance(images, list):
        raise GlbError("GLB has no material with an embedded base-color image")
    material = materials[0]
    pbr = material.get("pbrMetallicRoughness", {}) if isinstance(material, dict) else {}
    base_texture = pbr.get("baseColorTexture", {}) if isinstance(pbr, dict) else {}
    texture_index = base_texture.get("index") if isinstance(base_texture, dict) else None
    if isinstance(texture_index, bool) or not isinstance(texture_index, int) or not 0 <= texture_index < len(textures):
        raise GlbError("first GLB material does not reference a base-color texture")
    texture = textures[texture_index]
    image_index = texture.get("source") if isinstance(texture, dict) else None
    if isinstance(image_index, bool) or not isinstance(image_index, int) or not 0 <= image_index < len(images):
        raise GlbError("GLB base-color texture has an invalid image index")
    image = images[image_index]
    if not isinstance(image, dict):
        raise GlbError("GLB base-color image declaration is invalid")
    mime = image.get("mimeType")
    if mime not in ("image/png", "image/jpeg"):
        raise GlbError("GLB base-color image must use PNG or JPEG")

    if "bufferView" in image:
        views = document.get("bufferViews", [])
        view_index = image.get("bufferView")
        if (not isinstance(views, list) or isinstance(view_index, bool) or not isinstance(view_index, int) or
                not 0 <= view_index < len(views) or not isinstance(views[view_index], dict)):
            raise GlbError("GLB base-color image references an invalid bufferView")
        view = views[view_index]
        start = view.get("byteOffset", 0)
        length = view.get("byteLength")
        if (isinstance(start, bool) or not isinstance(start, int) or isinstance(length, bool) or
                not isinstance(length, int) or start < 0 or length <= 0 or start + length > len(binary)):
            raise GlbError("GLB base-color image bufferView is outside its BIN chunk")
        payload = binary[start:start + length]
    else:
        uri = image.get("uri")
        if not isinstance(uri, str) or not uri:
            raise GlbError("GLB base-color image has neither a bufferView nor a URI")
        if uri.startswith("data:"):
            header, separator, encoded = uri.partition(",")
            if not separator or ";base64" not in header or header[5:].split(";", 1)[0] != mime:
                raise GlbError("GLB base-color data URI must be a matching base64 PNG or JPEG")
            try:
                payload = base64.b64decode(encoded, validate=True)
            except (ValueError, binascii.Error) as error:
                raise GlbError("GLB base-color image data URI has invalid base64") from error
        else:
            parsed = urlsplit(uri)
            if parsed.scheme or parsed.netloc or parsed.query or parsed.fragment:
                raise GlbError("GLB base-color image URI must be local to the asset project")
            image_path = (source.parent / unquote(parsed.path)).resolve()
            if not image_path.is_relative_to(source.parent.resolve()) or not image_path.is_file():
                raise GlbError("GLB base-color image URI escapes the project or is missing")
            payload = image_path.read_bytes()

    signature = b"\x89PNG\r\n\x1a\n" if mime == "image/png" else b"\xff\xd8\xff"
    if len(payload) < len(signature) or not payload.startswith(signature):
        raise GlbError("GLB base-color image bytes do not match their declared PNG/JPEG type")
    return payload, ".png" if mime == "image/png" else ".jpg"


def blender_executable() -> str:
    configured = os.environ.get("BLENDER")
    if configured:
        executable = Path(configured).expanduser()
        if executable.is_file():
            return str(executable.resolve())
        raise GlbError(f"BLENDER points to a missing executable: {executable}")
    located = shutil.which("blender")
    if located:
        return located
    for candidate in (
        Path("/Applications/Blender.app/Contents/MacOS/Blender"),
    ):
        if candidate.is_file():
            return str(candidate)
    raise GlbError("GLB cooking requires Blender 4.0 or newer; install Blender or set BLENDER to its executable")


def main(arguments: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", nargs="?", type=Path)
    parser.add_argument("--asset-path", help="project-relative identity recorded in the cooked package")
    parser.add_argument("--output", type=Path, help="destination .pkg or .elpk file")
    parser.add_argument("--animation-source", type=Path, help="optional FBX whose clips match GLB bone names")
    parser.add_argument("--texture-output", type=Path, help="destination for the first material's base-color image")
    parser.add_argument("--texture-max-size", type=int,
        help="bound the extracted image to this many pixels per side (needs Pillow)")
    parser.add_argument("--max-triangles", type=int,
        help="simplify a static mesh to no more than this many triangles")
    parser.add_argument("--blender", help="Blender executable (or set BLENDER)")
    parser.add_argument("--self-test", action="store_true", help="validate the bounded GLB image reader")
    options = parser.parse_args(arguments)
    if not options.self_test and (options.source is None or options.asset_path is None or options.output is None):
        parser.error("source, --asset-path, and --output are required unless --self-test is used")

    try:
        if options.self_test:
            if (options.source is not None or options.output is not None or options.asset_path is not None or
                    options.animation_source is not None or options.texture_output is not None or
                    options.max_triangles is not None):
                parser.error("--self-test cannot be combined with asset paths or cooker options")
            self_test()
            return 0
        if options.max_triangles is not None and not 1 <= options.max_triangles <= 1000000:
            parser.error("--max-triangles must be in [1, 1000000]")
        source = options.source.expanduser().resolve(strict=True)
        if not source.is_file() or source.suffix.lower() != ".glb":
            raise GlbError("source must be a regular .glb file")
        if options.animation_source is not None:
            animation_source = options.animation_source.expanduser().resolve(strict=True)
            if not animation_source.is_file() or animation_source.suffix.lower() != ".fbx":
                raise GlbError("animation source must be a regular .fbx file")
        else:
            animation_source = None
        key = options.asset_path
        identity = PurePosixPath(key)
        if (not key or len(key) > 4096 or "\0" in key or "\n" in key or "\r" in key or
                identity.is_absolute() or "\\" in key or
                any(part in ("", ".", "..") for part in key.split("/"))):
            raise GlbError("asset path must be a safe project-relative identity")
        output = options.output.expanduser().resolve()
        if output == source or output == animation_source:
            raise GlbError("package output cannot overwrite an input asset")
        if options.texture_output is not None:
            texture_output = options.texture_output.expanduser().resolve()
            if texture_output in (source, animation_source, output):
                raise GlbError("texture output cannot overwrite an input asset or geometry package")
            if texture_output.suffix.lower() not in (".png", ".jpg", ".jpeg"):
                raise GlbError("texture output must have a .png, .jpg, or .jpeg suffix")
        else:
            texture_output = None
        if options.texture_max_size is not None and texture_output is None:
            raise GlbError("--texture-max-size requires --texture-output")

        document, binary = read_glb_document(source)
        extracted = base_color_image(source, document, binary) if texture_output is not None else None
        executable = Path(options.blender).expanduser().resolve() if options.blender else Path(blender_executable())
        if not executable.is_file():
            raise GlbError(f"Blender executable does not exist: {executable}")

        with tempfile.TemporaryDirectory(prefix="elisa-glb-cooker-") as temporary:
            temporary_path = Path(temporary)
            converted = temporary_path / "converted.fbx"
            command = [str(executable), "--background", "--factory-startup", "--python",
                str(ROOT / "scripts/cook_glb_asset_blender.py"), "--", "--source", str(source),
                "--output", str(converted)]
            if animation_source is not None:
                command.extend(["--animation-source", str(animation_source)])
            if options.max_triangles is not None:
                command.extend(["--max-triangles", str(options.max_triangles)])
            print("+", " ".join(repr(argument) for argument in command), flush=True)
            subprocess.run(command, cwd=ROOT, check=True)
            if not converted.is_file() or converted.stat().st_size == 0:
                raise GlbError("Blender did not create a converted FBX")
            cooker = ROOT / "scripts/cook_fbx_asset.py"
            cook_command = [sys.executable, str(cooker), str(converted), "--asset-path", key, "--output", str(output)]
            if options.max_triangles is not None:
                cook_command.extend(["--max-triangles", str(options.max_triangles)])
            print("+", " ".join(repr(argument) for argument in cook_command), flush=True)
            subprocess.run(cook_command, cwd=ROOT, check=True)

        if extracted is not None and texture_output is not None:
            payload, extension = extracted
            if (extension == ".png") != (texture_output.suffix.lower() == ".png"):
                raise GlbError("texture output suffix does not match the GLB image type")
            if options.texture_max_size is not None:
                try:
                    payload, width, height = cook_image_asset.resample_image_bytes(
                        payload, extension, options.texture_max_size)
                except cook_image_asset.ImageCookError as error:
                    raise GlbError(f"texture output could not be bounded: {error}") from error
                print(f"Bounded GLB base-color texture to {width}x{height}")
            texture_output.parent.mkdir(parents=True, exist_ok=True)
            texture_output.write_bytes(payload)
            print(f"Extracted GLB base-color texture: {texture_output} ({len(payload)} bytes)")
    except (OSError, GlbError, subprocess.CalledProcessError) as error:
        print(f"GLB cooking failed: {error}", file=sys.stderr)
        return 1
    return 0


def self_test() -> None:
    image = b"\x89PNG\r\n\x1a\nself-test-image"
    document = {
        "asset": {"version": "2.0"},
        "buffers": [{"byteLength": len(image)}],
        "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": len(image)}],
        "images": [{"mimeType": "image/png", "bufferView": 0}],
        "textures": [{"source": 0}],
        "materials": [{"pbrMetallicRoughness": {"baseColorTexture": {"index": 0}}}],
    }
    json_chunk = json.dumps(document, separators=(",", ":")).encode("utf-8")
    json_chunk += b" " * ((-len(json_chunk)) % 4)
    binary_chunk = image + b"\0" * ((-len(image)) % 4)
    total = 12 + 8 + len(json_chunk) + 8 + len(binary_chunk)
    glb = (struct.pack("<4sII", b"glTF", 2, total) +
        struct.pack("<II", len(json_chunk), GLB_JSON) + json_chunk +
        struct.pack("<II", len(binary_chunk), GLB_BIN) + binary_chunk)
    with tempfile.TemporaryDirectory(prefix="elisa-glb-cooker-test-") as temporary:
        source = Path(temporary) / "fixture.glb"
        source.write_bytes(glb)
        loaded, binary = read_glb_document(source)
        payload, extension = base_color_image(source, loaded, binary)
        if payload != image or extension != ".png":
            raise GlbError("GLB self-test did not recover the embedded base-color image")
        malformed = Path(temporary) / "malformed.glb"
        malformed.write_bytes(glb[:-1])
        try:
            read_glb_document(malformed)
        except GlbError:
            pass
        else:
            raise GlbError("GLB self-test accepted a truncated file")
    print("GLB cooker self-test passed: bounded container parsing and embedded base-color extraction")


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
