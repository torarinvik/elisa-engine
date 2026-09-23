#!/usr/bin/env python3
"""Cook one bounded FBX mesh into Elisa's existing cooked geometry package."""

from __future__ import annotations

import argparse
import base64
import binascii
import hashlib
import math
import os
from pathlib import Path, PurePosixPath
import re
import struct
import subprocess
import sys
import tempfile

import cook_gltf_animation
from elisa_package import encoded_image_dimensions, write_geometry_package
from fbx_material_cooker_self_test import validate_material_package
from fbx_surface_texture import decode_png_rgba, pack_surface_maps
from fbx_surface_texture_self_test import validate_surface_texture_decoder
from fbx_test_fixtures import write_two_material_mesh, write_two_mesh_scene
from png_image import encode_png


ROOT = Path(__file__).resolve().parents[1]
MAX_PACKAGE_BYTES = 64 * 1024 * 1024
MAX_LINE_BYTES = 16 * 1024 * 1024
MAX_FILE_BYTES = 512 * 1024 * 1024
MAX_MATERIAL_SLOTS = 16
MAX_SOURCE_MESH_COUNT = 1024


def run(command: list[str]) -> None:
    print("+", " ".join(repr(argument) for argument in command), flush=True)
    subprocess.run(command, cwd=ROOT, check=True)


def run_capture(command: list[str]) -> str:
    """Run one cook while retaining its deterministic optimization report."""
    print("+", " ".join(repr(argument) for argument in command), flush=True)
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, check=False)
    sys.stdout.write(result.stdout)
    sys.stderr.write(result.stderr)
    if result.returncode != 0:
        raise subprocess.CalledProcessError(result.returncode, command, result.stdout, result.stderr)
    return result.stdout


def source_hash(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def validate_asset_key(value: str) -> str:
    path = PurePosixPath(value)
    if (not value or len(value) > 4096 or path.is_absolute() or "\\" in value or
            "\n" in value or "\r" in value or "\0" in value or
            any(part in ("", ".", "..") for part in value.split("/"))):
        raise ValueError("asset path must be a safe project-relative path without `..`")
    return value


def parse_package(path: Path, expected_source: str, expected_hash: str) -> dict[str, str]:
    if not path.is_file() or path.stat().st_size > MAX_PACKAGE_BYTES:
        raise ValueError("cooked package is missing or exceeds the 64 MiB reader limit")
    fields: dict[str, str] = {}
    for line in path.read_text(encoding="ascii").splitlines():
        if len(line.encode("ascii")) > MAX_LINE_BYTES:
            raise ValueError("cooked package section exceeds the 16 MiB reader limit")
        key, separator, value = line.partition("=")
        if not separator or not key or key in fields:
            raise ValueError("cooked package has a malformed or duplicate section")
        fields[key] = value
    required = {
        "format", "source", "source_sha256", "triangles", "positions", "indices",
        "position_stride", "normal_stride", "uv_stride", "index_stride",
        "tangent_stride", "positions_b64", "normals_b64", "uvs_b64", "tangents_b64", "indices_b64",
    }
    if not required.issubset(fields):
        raise ValueError("cooked package is missing normalized geometry sections")
    if fields["format"] not in ("elisa-cooked-v2", "elisa-cooked-v3") or fields["source"] != expected_source:
        raise ValueError("cooked package format or source identity does not match")
    has_rig_format = fields["format"] == "elisa-cooked-v3"
    if fields["source_sha256"] != expected_hash:
        raise ValueError("cooked package source hash does not match the input FBX")
    try:
        source_mesh_count = int(fields.get("source_mesh_count", "1"))
    except ValueError as failure:
        raise ValueError("cooked package has an invalid source mesh count") from failure
    if not 1 <= source_mesh_count <= MAX_SOURCE_MESH_COUNT:
        raise ValueError("cooked package has an unsupported source mesh count")
    if (fields["position_stride"], fields["normal_stride"], fields["uv_stride"],
            fields["tangent_stride"], fields["index_stride"]) != ("12", "12", "8", "16", "4"):
        raise ValueError("cooked package has unsupported geometry strides")

    def decode(name: str) -> bytes:
        try:
            return base64.b64decode(fields[name], validate=True)
        except (ValueError, binascii.Error) as failure:
            raise ValueError(f"cooked package has invalid base64 in {name}") from failure

    positions = decode("positions_b64")
    normals = decode("normals_b64")
    uvs = decode("uvs_b64")
    tangents = decode("tangents_b64")
    indices = decode("indices_b64")
    try:
        triangles = int(fields["triangles"])
        vertex_count = int(fields["positions"])
        index_count = int(fields["indices"])
    except ValueError as failure:
        raise ValueError("cooked package has invalid geometry counts") from failure
    if (triangles <= 0 or vertex_count <= 0 or index_count != triangles * 3 or
            len(positions) != vertex_count * 12 or len(normals) != vertex_count * 12 or
            len(uvs) != vertex_count * 8 or len(tangents) != vertex_count * 16 or
            len(indices) != index_count * 4):
        raise ValueError("cooked package geometry counts and byte lengths disagree")
    if not all(math.isfinite(value) for (value,) in struct.iter_unpack("<f", positions + normals + uvs + tangents)):
        raise ValueError("cooked package contains non-finite geometry")
    for vertex in range(vertex_count):
        tangent = struct.unpack_from("<4f", tangents, vertex * 16)
        normal = struct.unpack_from("<3f", normals, vertex * 12)
        tangent_length = math.sqrt(sum(component * component for component in tangent[:3]))
        alignment = sum(tangent[index] * normal[index] for index in range(3))
        if abs(tangent_length - 1.0) > 0.02 or abs(alignment) > 0.02 or abs(abs(tangent[3]) - 1.0) > 1e-4:
            raise ValueError("cooked package contains an invalid tangent frame")
    if any(value >= vertex_count for (value,) in struct.iter_unpack("<I", indices)):
        raise ValueError("cooked package contains an out-of-range index")
    subset_fields = {"material_slots", "subset_count", "subset_stride", "subsets_b64"}
    present_subset_fields = subset_fields.intersection(fields)
    if present_subset_fields and present_subset_fields != subset_fields:
        raise ValueError("cooked package has incomplete material subset metadata")
    if present_subset_fields:
        try:
            material_slots = int(fields["material_slots"])
            subset_count = int(fields["subset_count"])
        except ValueError as failure:
            raise ValueError("cooked package has invalid material subset counts") from failure
        subset_data = decode("subsets_b64")
        if not 1 <= material_slots <= MAX_MATERIAL_SLOTS or not 1 <= subset_count <= MAX_MATERIAL_SLOTS or \
                fields["subset_stride"] != "12" or len(subset_data) != subset_count * 12:
            raise ValueError("cooked package has unsupported material subset bounds or stride")
        subset_words = struct.unpack(f"<{subset_count * 3}I", subset_data)
        next_index = 0
        for subset in range(subset_count):
            start, count, slot = subset_words[subset * 3:subset * 3 + 3]
            if start != next_index or count == 0 or count % 3 or count > index_count - start or slot >= material_slots:
                raise ValueError("cooked package material subsets do not partition the index stream")
            next_index += count
        if next_index != index_count:
            raise ValueError("cooked package material subsets do not cover the index stream")
    slot_material_fields = {"slot_material_stride", "slot_materials_b64", "slot_material_names_b64"}
    present_slot_material_fields = slot_material_fields.intersection(fields)
    if present_slot_material_fields and present_slot_material_fields != slot_material_fields:
        raise ValueError("cooked package has incomplete FBX material records")
    if present_slot_material_fields:
        if not present_subset_fields or fields["slot_material_stride"] != "48":
            raise ValueError("cooked package has unsupported FBX material slots or stride")
        material_data = decode("slot_materials_b64")
        if len(material_data) != material_slots * 48:
            raise ValueError("cooked package FBX material count does not match its slots")
        for slot in range(material_slots):
            record = struct.unpack_from("<10f2I", material_data, slot * 48)
            if (not all(math.isfinite(value) and 0.0 <= value <= 1.0 for value in record[:10]) or
                    record[10] > 2 or record[11] & ~3):
                raise ValueError("cooked package contains out-of-range FBX material factors")
        names_data = decode("slot_material_names_b64")
        offset = 0
        for _ in range(material_slots):
            if len(names_data) - offset < 4:
                raise ValueError("cooked package FBX material-name stream is truncated")
            (length,) = struct.unpack_from("<I", names_data, offset)
            offset += 4
            if length > 256 or length > len(names_data) - offset:
                raise ValueError("cooked package FBX material name exceeds its limit")
            try:
                name = names_data[offset:offset + length].decode("utf-8", errors="strict")
            except UnicodeDecodeError as failure:
                raise ValueError("cooked package FBX material name is not UTF-8") from failure
            if "\0" in name:
                raise ValueError("cooked package FBX material name contains a null byte")
            offset += length
        if offset != len(names_data):
            raise ValueError("cooked package FBX material-name stream has trailing bytes")
    texture_source_fields = {"texture_source_count", "texture_source_names_b64", "slot_texture_sources_b64"}
    present_texture_source_fields = texture_source_fields.intersection(fields)
    if present_texture_source_fields and present_texture_source_fields != texture_source_fields:
        raise ValueError("cooked package has incomplete FBX texture source metadata")
    if "slot_surface_texture_sources_b64" in fields and not present_texture_source_fields:
        raise ValueError("FBX surface-map references require texture source metadata")
    if present_texture_source_fields:
        if not present_subset_fields or not present_slot_material_fields:
            raise ValueError("FBX texture sources require material slots and material records")
        try:
            texture_source_count = int(fields["texture_source_count"])
        except ValueError as failure:
            raise ValueError("cooked package has an invalid FBX texture source count") from failure
        if not 1 <= texture_source_count <= 64:
            raise ValueError("cooked package exceeds the 64 FBX texture source limit")
        names_data = decode("texture_source_names_b64")
        texture_source_names: list[str] = []
        offset = 0
        for _ in range(texture_source_count):
            if len(names_data) - offset < 4:
                raise ValueError("cooked package FBX texture-name stream is truncated")
            (length,) = struct.unpack_from("<I", names_data, offset)
            offset += 4
            if length == 0 or length > 4096 or length > len(names_data) - offset:
                raise ValueError("cooked package FBX texture path exceeds its limit")
            try:
                name = names_data[offset:offset + length].decode("utf-8", errors="strict")
            except UnicodeDecodeError as failure:
                raise ValueError("cooked package FBX texture path is not UTF-8") from failure
            offset += length
            validate_texture_source_path(name)
            if name in texture_source_names:
                raise ValueError("cooked package has duplicate FBX texture source paths")
            texture_source_names.append(name)
        if offset != len(names_data):
            raise ValueError("cooked package FBX texture-name stream has trailing bytes")
        source_references = decode("slot_texture_sources_b64")
        if len(source_references) != material_slots * 5 * 4:
            raise ValueError("cooked package FBX texture references do not match its material slots")
        references = struct.unpack(f"<{material_slots * 5}I", source_references)
        if "slot_surface_texture_sources_b64" in fields:
            surface_source_data = decode("slot_surface_texture_sources_b64")
            if len(surface_source_data) != material_slots * 2 * 4:
                raise ValueError("cooked package FBX surface-map references do not match its material slots")
            surface_source_references = struct.unpack(f"<{material_slots * 2}I", surface_source_data)
        else:
            surface_source_references = (0,) * (material_slots * 2)
        sampled_sources: set[int] = set()
        for reference in (*references, *surface_source_references):
            if reference > texture_source_count:
                raise ValueError("cooked package FBX material references a missing texture path")
            if reference:
                sampled_sources.add(reference)
        if len(sampled_sources) != texture_source_count:
            raise ValueError("cooked package contains an unused FBX texture source")
    skin_fields = {"skin_bones", "skin_indices_stride", "skin_weights_stride",
        "skin_indices_b64", "skin_weights_b64", "skin_names_b64"}
    present_skin_fields = skin_fields.intersection(fields)
    if present_skin_fields and present_skin_fields != skin_fields:
        raise ValueError("cooked package has an incomplete skin payload")
    if present_skin_fields:
        try:
            bone_count = int(fields["skin_bones"])
        except ValueError as failure:
            raise ValueError("cooked package has an invalid skin bone count") from failure
        if not 1 <= bone_count <= 64 or fields["skin_indices_stride"] != "16" or fields["skin_weights_stride"] != "16":
            raise ValueError("cooked package has unsupported skin bounds or strides")
        skin_indices = decode("skin_indices_b64")
        skin_weights = decode("skin_weights_b64")
        skin_names = decode("skin_names_b64")
        if len(skin_indices) != vertex_count * 16 or len(skin_weights) != vertex_count * 16:
            raise ValueError("cooked package skin influence lengths do not match the mesh")
        if not all(math.isfinite(value) for (value,) in struct.iter_unpack("<f", skin_weights)):
            raise ValueError("cooked package contains non-finite skin weights")
        for vertex in range(vertex_count):
            joint_indices = struct.unpack_from("<4I", skin_indices, vertex * 16)
            weights = struct.unpack_from("<4f", skin_weights, vertex * 16)
            if any(weight < 0.0 or (weight > 0.0 and joint >= bone_count)
                    for joint, weight in zip(joint_indices, weights)) or abs(sum(weights) - 1.0) > 0.005:
                raise ValueError("cooked package contains invalid or unnormalized skin influences")
        offset = 0
        for _ in range(bone_count):
            if len(skin_names) - offset < 4:
                raise ValueError("cooked package bone-name stream is truncated")
            (length,) = struct.unpack_from("<I", skin_names, offset)
            offset += 4
            if length > len(skin_names) - offset:
                raise ValueError("cooked package bone name exceeds its stream")
            offset += length
        if offset != len(skin_names):
            raise ValueError("cooked package bone-name stream has trailing bytes")

    rig_fields = {"skin_joints", "skin_joint_parent_stride", "skin_joint_rest_stride",
        "skin_joint_parents_b64", "skin_joint_rest_b64", "skin_joint_names_b64",
        "skin_cluster_joints_stride", "skin_cluster_joints_b64"}
    present_rig_fields = rig_fields.intersection(fields)
    if has_rig_format != bool(present_rig_fields) or (present_rig_fields and present_rig_fields != rig_fields) or \
            (present_rig_fields and not present_skin_fields):
        raise ValueError("cooked package has an incomplete rig hierarchy")
    if present_rig_fields:
        try:
            joint_count = int(fields["skin_joints"])
            cluster_count = int(fields["skin_bones"])
        except ValueError as failure:
            raise ValueError("cooked package has invalid rig counts") from failure
        if not 1 <= joint_count <= 64 or fields["skin_joint_parent_stride"] != "4" or \
                fields["skin_joint_rest_stride"] != "40" or fields["skin_cluster_joints_stride"] != "4":
            raise ValueError("cooked package has unsupported rig bounds or strides")
        parents = decode("skin_joint_parents_b64")
        rests = decode("skin_joint_rest_b64")
        names = decode("skin_joint_names_b64")
        cluster_joints = decode("skin_cluster_joints_b64")
        if len(parents) != joint_count * 4 or len(rests) != joint_count * 40 or \
                len(cluster_joints) != cluster_count * 4:
            raise ValueError("cooked package rig stream lengths do not match the hierarchy")

        def decode_names(data: bytes, count: int) -> list[bytes]:
            result: list[bytes] = []
            offset = 0
            for _ in range(count):
                if len(data) - offset < 4:
                    raise ValueError("cooked package joint-name stream is truncated")
                (length,) = struct.unpack_from("<I", data, offset)
                offset += 4
                if length > len(data) - offset:
                    raise ValueError("cooked package joint name exceeds its stream")
                result.append(data[offset:offset + length])
                offset += length
            if offset != len(data):
                raise ValueError("cooked package joint-name stream has trailing bytes")
            return result

        joint_names = decode_names(names, joint_count)
        skin_cluster_names = decode_names(skin_names, cluster_count)
        parent_values = struct.unpack(f"<{joint_count}i", parents)
        joint_palette = struct.unpack(f"<{cluster_count}I", cluster_joints)
        seen_clusters: set[int] = set()
        for joint_index, parent in enumerate(parent_values):
            if parent < -1 or parent >= joint_index:
                raise ValueError("cooked package rig is not parent ordered")
            rest = struct.unpack_from("<10f", rests, joint_index * 40)
            rotation_length = math.sqrt(sum(value * value for value in rest[3:7]))
            if not all(math.isfinite(value) for value in rest) or not 0.99 < rotation_length < 1.01 or \
                    any(value == 0.0 for value in rest[7:10]):
                raise ValueError("cooked package contains an invalid joint rest transform")
        for cluster_index, joint_index in enumerate(joint_palette):
            if joint_index >= joint_count or joint_index in seen_clusters or \
                    joint_names[joint_index] != skin_cluster_names[cluster_index]:
                raise ValueError("cooked package cluster map does not match its rig hierarchy")
            seen_clusters.add(joint_index)

        try:
            clip_count = int(fields["animation_clips"])
        except (KeyError, ValueError) as failure:
            raise ValueError("cooked package has an invalid animation clip count") from failure
        if not 0 <= clip_count <= cook_gltf_animation.MAX_CLIPS:
            raise ValueError("cooked package exceeds the animation clip limit")
        total_sample_floats = 0
        for clip_index in range(clip_count):
            prefix = f"animation_{clip_index}_"
            try:
                clip_name_data = decode(prefix + "name_b64")
                (name_length,) = struct.unpack_from("<I", clip_name_data)
                if name_length == 0 or name_length != len(clip_name_data) - 4:
                    raise ValueError("cooked package has an invalid animation name")
                duration = float(fields[prefix + "duration_seconds"])
                sample_rate = int(fields[prefix + "sample_rate"])
                frame_count = int(fields[prefix + "frames"])
                transform_stride = fields[prefix + "transform_stride"]
            except (KeyError, ValueError, struct.error) as failure:
                raise ValueError("cooked package has invalid animation metadata") from failure
            if not math.isfinite(duration) or duration <= 0.0 or not 1 <= sample_rate <= 120 or \
                    not 2 <= frame_count <= 3601 or transform_stride != "40":
                raise ValueError("cooked package has unsupported animation bounds or strides")
            sample_floats = joint_count * frame_count * 10
            if sample_floats > 2_000_000 - total_sample_floats:
                raise ValueError("cooked package exceeds the bounded animation sample budget")
            sample_data = decode(prefix + "samples_b64")
            if len(sample_data) != sample_floats * 4:
                raise ValueError("cooked package animation sample length is invalid")
            for offset in range(0, sample_floats, 10):
                sample = struct.unpack_from("<10f", sample_data, offset * 4)
                rotation_length = math.sqrt(sum(value * value for value in sample[3:7]))
                if not all(math.isfinite(value) for value in sample) or not 0.99 < rotation_length < 1.01 or \
                        any(value == 0.0 for value in sample[7:10]):
                    raise ValueError("cooked package contains an invalid animation sample")
            total_sample_floats += sample_floats
    return fields


def validate_texture_source_path(value: str) -> None:
    """Require a portable relative FBX texture path with no traversal."""
    if (not value or len(value.encode("utf-8")) > 4096 or value.startswith("/") or
            "\\" in value or ":" in value or "\0" in value or "\r" in value or "\n" in value or
            any(part in ("", ".", "..") for part in value.split("/"))):
        raise ValueError("FBX texture source must be a safe relative path without parent traversal")


def package_fbx_texture_sources(source: Path, geometry_package: bytes,
    fields: dict[str, str]) -> tuple[bytes, dict[str, bytes]]:
    """Resolve external FBX images and translate their source paths to ELPK sections."""
    if "texture_source_count" not in fields:
        return geometry_package, {}
    count = int(fields["texture_source_count"])
    names_data = base64.b64decode(fields["texture_source_names_b64"], validate=True)
    source_names: list[str] = []
    offset = 0
    for _ in range(count):
        (length,) = struct.unpack_from("<I", names_data, offset)
        offset += 4
        source_names.append(names_data[offset:offset + length].decode("utf-8", errors="strict"))
        offset += length
    texture_root = source.resolve(strict=True).parent
    source_images: list[bytes] = []
    for index, name in enumerate(source_names):
        validate_texture_source_path(name)
        image_path = (texture_root / PurePosixPath(name)).resolve(strict=True)
        try:
            image_path.relative_to(texture_root)
        except ValueError as failure:
            raise ValueError("FBX texture resolves outside the source directory") from failure
        if not image_path.is_file() or image_path.stat().st_size > MAX_PACKAGE_BYTES:
            raise ValueError("FBX texture is missing, not a regular file, or exceeds the image-size limit")
        image = image_path.read_bytes()
        encoded_image_dimensions(image)
        source_images.append(image)
    references = base64.b64decode(fields["slot_texture_sources_b64"], validate=True)
    material_slots = int(fields["material_slots"])
    if len(references) != material_slots * 5 * 4:
        raise ValueError("FBX texture references do not match their material slots")
    direct_references = struct.unpack(f"<{material_slots * 5}I", references)
    encoded_surface_references = fields.get("slot_surface_texture_sources_b64")
    if encoded_surface_references is not None:
        surface_reference_data = base64.b64decode(encoded_surface_references, validate=True)
        if len(surface_reference_data) != material_slots * 2 * 4:
            raise ValueError("FBX surface-map references do not match their material slots")
        surface_references = struct.unpack(f"<{material_slots * 2}I", surface_reference_data)
    else:
        surface_references = (0,) * (material_slots * 2)
    all_source_references = (*direct_references, *surface_references)
    if any(reference > len(source_images) for reference in all_source_references):
        raise ValueError("FBX material references a missing texture source image")
    if {reference for reference in all_source_references if reference} != set(range(1, len(source_images) + 1)):
        raise ValueError("FBX package contains an unused texture source image")

    images: dict[str, bytes] = {}
    image_indices: dict[bytes, int] = {}
    runtime_references = [0] * (material_slots * 5)
    referenced_direct = {reference - 1 for reference in direct_references if reference}
    source_to_runtime: dict[int, int] = {}
    for source_index, image in enumerate(source_images):
        if source_index not in referenced_direct:
            continue
        runtime_index = image_indices.get(image)
        if runtime_index is None:
            section = f"fbx_image_{len(images)}"
            images[section] = image
            runtime_index = len(images)
            image_indices[image] = runtime_index
        source_to_runtime[source_index + 1] = runtime_index
    for slot, reference in enumerate(direct_references):
        if reference:
            runtime_references[slot] = source_to_runtime[reference]

    for slot in range(material_slots):
        roughness_reference, metalness_reference = surface_references[slot * 2:slot * 2 + 2]
        if not roughness_reference and not metalness_reference:
            continue
        if direct_references[slot * 5 + 2]:
            raise ValueError("FBX material cannot combine a packed surface map with separate roughness or metalness maps")
        packed = pack_surface_maps(
            source_images[roughness_reference - 1] if roughness_reference else None,
            source_images[metalness_reference - 1] if metalness_reference else None)
        runtime_index = image_indices.get(packed)
        if runtime_index is None:
            section = f"fbx_image_{len(images)}"
            images[section] = packed
            runtime_index = len(images)
            image_indices[packed] = runtime_index
        runtime_references[slot * 5 + 2] = runtime_index
    if not images or len(images) > 64:
        raise ValueError("cooked FBX material images exceed the 64-image package limit")
    references = struct.pack(f"<{len(runtime_references)}I", *runtime_references)
    texture_names = b"".join(struct.pack("<I", len(name.encode("ascii"))) + name.encode("ascii")
        for name in images)
    runtime_fields = {
        "texture_count": str(len(images)),
        "texture_names_b64": base64.b64encode(texture_names).decode("ascii"),
        "slot_texture_stride": "20",
        "slot_textures_b64": base64.b64encode(references).decode("ascii"),
    }
    source_fields = {"texture_source_count", "texture_source_names_b64", "slot_texture_sources_b64",
        "slot_surface_texture_sources_b64"}
    lines = [line for line in geometry_package.decode("ascii").splitlines()
        if line.partition("=")[0] not in source_fields]
    lines.extend(f"{key}={value}" for key, value in runtime_fields.items())
    return ("\n".join(lines) + "\n").encode("ascii"), images


def build_cooker(build_dir: Path) -> Path:
    dependency = ROOT / "dependencies/ufbx"
    meshoptimizer = ROOT / "dependencies/meshoptimizer"
    mikktspace = ROOT / "dependencies/mikktspace"
    source = dependency / "ufbx.c"
    header = dependency / "ufbx.h"
    if not source.is_file() or not header.is_file():
        raise ValueError("missing pinned ufbx files; run python3 scripts/fetch_dependencies.py")
    mikktspace_source = mikktspace / "mikktspace.c"
    mikktspace_header = mikktspace / "mikktspace.h"
    if not mikktspace_source.is_file() or not mikktspace_header.is_file():
        raise ValueError("missing pinned MikkTSpace files; run python3 scripts/fetch_dependencies.py")
    simplifier = meshoptimizer / "simplifier.cpp"
    vcache = meshoptimizer / "vcacheoptimizer.cpp"
    analyzer = meshoptimizer / "indexanalyzer.cpp"
    vfetch = meshoptimizer / "vfetchoptimizer.cpp"
    indexgenerator = meshoptimizer / "indexgenerator.cpp"
    if not all(path.is_file() for path in (meshoptimizer / "meshoptimizer.h", simplifier, vcache, analyzer, vfetch, indexgenerator)):
        raise ValueError("missing pinned meshoptimizer stages; run python3 scripts/fetch_dependencies.py")
    cc = os.environ.get("CC", "cc")
    cxx = os.environ.get("CXX", "c++")
    object_file = build_dir / "ufbx.o"
    mikktspace_object_file = build_dir / "mikktspace.o"
    executable = build_dir / "fbx-asset-cooker"
    run([cc, "-std=c99", "-O2", "-I", str(dependency), "-c", str(source), "-o", str(object_file)])
    run([cc, "-std=c99", "-O2", "-I", str(mikktspace), "-c", str(mikktspace_source),
        "-o", str(mikktspace_object_file)])
    run([cxx, "-std=c++17", "-O2", "-I", str(dependency), "-I", str(meshoptimizer),
        "-I", str(mikktspace), "-I", str(ROOT / "native"),
        str(ROOT / "native/fbx_asset_cooker.cpp"), str(simplifier), str(vcache), str(analyzer),
        str(vfetch), str(indexgenerator),
        str(meshoptimizer / "allocator.cpp"), str(object_file), str(mikktspace_object_file),
        "-o", str(executable)])
    return executable


def write_grid_fixture(path: Path, cells_per_side: int, two_materials: bool = False) -> None:
    """Write a small planar FBX grid that exercises the real simplification path."""
    vertex_count = (cells_per_side + 1) ** 2
    positions = []
    uvs = []
    for y in range(cells_per_side + 1):
        for x in range(cells_per_side + 1):
            positions.extend((str(x), str(y), "0"))
            uvs.extend((str(x / cells_per_side), str(y / cells_per_side)))
    polygon_indices = []
    material_assignments = []
    for y in range(cells_per_side):
        for x in range(cells_per_side):
            top_left = y * (cells_per_side + 1) + x
            top_right = top_left + 1
            bottom_left = top_left + cells_per_side + 1
            bottom_right = bottom_left + 1
            for triangle in ((top_left, top_right, bottom_right),
                    (top_left, bottom_right, bottom_left)):
                polygon_indices.extend(str(index) for index in triangle[:-1])
                polygon_indices.append(str(-triangle[-1] - 1))
                material_assignments.append(str(int(x >= cells_per_side // 2) if two_materials else 0))

    material_layer = ""
    material_layer_reference = ""
    material_objects = ""
    material_connections = ""
    material_definition = ""
    definition_count = 2
    if two_materials:
        definition_count = 4
        material_definition = 'ObjectType: "Material" { Count: 2 } '
        material_layer = (
            f'LayerElementMaterial: 0 {{ Version: 101 Name: "" MappingInformationType: "ByPolygon" '
            f'ReferenceInformationType: "IndexToDirect" Materials: *{len(material_assignments)} {{ a: '
            f'{",".join(material_assignments)} }} }} ')
        material_layer_reference = (
            ' LayerElement: { Type: "LayerElementMaterial" TypedIndex: 0 }')
        material_objects = (
            'Material: 1103, "Material::First", "" { Version: 102 } '
            'Material: 1104, "Material::Second", "" { Version: 102 } ')
        material_connections = ' C: "OO",1103,1002 C: "OO",1104,1002'

    path.write_text(
        '; FBX 7.4.0 project file\n'
        'FBXHeaderExtension: { FBXHeaderVersion: 1003 FBXVersion: 7400 }\n'
        'GlobalSettings: { Version: 1000 Properties70: { '
        'P: "UpAxis", "int", "Integer", "", 1 '
        'P: "UpAxisSign", "int", "Integer", "", 1 '
        'P: "FrontAxis", "int", "Integer", "", 2 '
        'P: "FrontAxisSign", "int", "Integer", "", 1 '
        'P: "CoordAxis", "int", "Integer", "", 0 '
        'P: "CoordAxisSign", "int", "Integer", "", 1 '
        'P: "UnitScaleFactor", "double", "Number", "", 1 } }\n'
        f'Definitions: {{ Version: 100 Count: {definition_count} '
        'ObjectType: "Geometry" { Count: 1 } ObjectType: "Model" { Count: 1 } '
        + material_definition + '}\n'
        'Objects: { '
        f'Geometry: 1001, "Geometry::Grid", "Mesh" {{ '
        f'GeometryVersion: 124 Vertices: *{vertex_count * 3} {{ a: '
        f'{",".join(positions)} }} '
        f'PolygonVertexIndex: *{len(polygon_indices)} {{ a: '
        f'{",".join(polygon_indices)} }} {material_layer}'
        f'LayerElementUV: 0 {{ Version: 101 Name: "UVMap" '
        f'MappingInformationType: "ByVertice" ReferenceInformationType: "Direct" '
        f'UV: *{len(uvs)} {{ a: {",".join(uvs)} }} }} '
        'Layer: 0 { Version: 100 LayerElement: { Type: "LayerElementUV" TypedIndex: 0 }'
        f'{material_layer_reference} }} }} '
        f'Model: 1002, "Model::Grid", "Mesh" {{ Version: 232 }} {material_objects}}}\n'
        f'Connections: {{ C: "OO",1001,1002 C: "OO",1002,0{material_connections} }}\n'
        'Takes: { Current: "" }\n',
        encoding="ascii")


def cook_one(cooker: Path, source: Path, asset_path: str, output: Path,
    max_triangles: int | None = None, mesh_name: str | None = None,
    report: list[str] | None = None, ignore_material_textures: bool = False,
    all_meshes: bool = False) -> dict[str, str]:
    source = source.expanduser().resolve(strict=True)
    if not source.is_file():
        raise ValueError("FBX source must be a regular file")
    if source.stat().st_size == 0 or source.stat().st_size > MAX_FILE_BYTES:
        raise ValueError("FBX source is empty or exceeds the 512 MiB source limit")
    key = validate_asset_key(asset_path)
    digest = source_hash(source)
    output = output.expanduser().resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    command = [str(cooker), "--source", str(source), "--asset-path", key,
        "--output", str(output), "--sha256", digest]
    if max_triangles is not None:
        if isinstance(max_triangles, bool) or not 1 <= max_triangles <= 1000000:
            raise ValueError("max triangles must be an integer in [1, 1000000]")
        command.extend(["--max-triangles", str(max_triangles)])
    if mesh_name is not None:
        if not mesh_name or len(mesh_name.encode("utf-8")) > 512 or "\0" in mesh_name or "\n" in mesh_name or "\r" in mesh_name:
            raise ValueError("mesh name must be 1 to 512 UTF-8 bytes without line breaks")
        command.extend(["--mesh-name", mesh_name])
    if ignore_material_textures:
        command.append("--ignore-material-textures")
    if all_meshes:
        if mesh_name is not None:
            raise ValueError("all meshes cannot be combined with an exact mesh selector")
        command.append("--all-meshes")
    output_text = run_capture(command)
    if report is not None:
        report.append(output_text)
    return parse_package(output, key, digest)


def main(arguments: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", nargs="?", type=Path, help="source FBX file")
    parser.add_argument("--asset-path", help="project-relative identity recorded in the cooked package")
    parser.add_argument("--output", type=Path, help="destination .pkg or .elpk path")
    parser.add_argument("--max-triangles", type=int, help="simplify output to no more than this many triangles")
    parser.add_argument("--mesh-name", help="select one FBX node or mesh by its exact name")
    parser.add_argument("--ignore-material-textures", action="store_true",
        help="ignore FBX image references when the application supplies textures separately")
    parser.add_argument("--all-meshes", action="store_true",
        help="combine all static triangle meshes in the FBX scene into one cooked mesh")
    parser.add_argument("--dependency", action="append", default=[], metavar="BUNDLE",
        help="name a bundle this .elpk needs, relative to the output's directory")
    parser.add_argument("--self-test", action="store_true", help="cook and validate the synthetic triangle fixture")
    options = parser.parse_args(arguments)
    if not options.self_test and (options.source is None or options.asset_path is None or options.output is None):
        parser.error("source, --asset-path, and --output are required unless --self-test is used")
    if options.self_test and (options.source is not None or options.asset_path is not None or options.output is not None or
            options.max_triangles is not None or options.mesh_name is not None or options.all_meshes or
            options.ignore_material_textures):
        parser.error("--self-test cannot be combined with source, --asset-path, --output, or cooker options")
    if options.max_triangles is not None and not 1 <= options.max_triangles <= 1000000:
        parser.error("--max-triangles must be in [1, 1000000]")
    if options.all_meshes and options.mesh_name is not None:
        parser.error("--all-meshes cannot be combined with --mesh-name")
    if options.dependency and (options.self_test or options.output.suffix.lower() != ".elpk"):
        parser.error("--dependency requires an .elpk output bundle")

    try:
        with tempfile.TemporaryDirectory(prefix="elisa-fbx-cooker-") as temporary:
            directory = Path(temporary)
            cooker = build_cooker(directory)
            if options.self_test:
                validate_surface_texture_decoder()
                tangent_test = subprocess.run([sys.executable,
                    str(ROOT / "scripts/test_mikktspace.py")], check=False)
                if tangent_test.returncode != 0:
                    return tangent_test.returncode
                source = ROOT / "test/fixtures/fbx_triangle.fbx"
                output = directory / "triangle.pkg"
                fields = cook_one(cooker, source, "test/fixtures/fbx_triangle.fbx", output)
                if int(fields["triangles"]) != 1 or int(fields["positions"]) != 3:
                    raise ValueError("triangle fixture package counts do not match")
                multi_mesh_source = directory / "two-mesh-scene.fbx"
                write_two_mesh_scene(multi_mesh_source)
                multi_mesh_fields = cook_one(cooker, multi_mesh_source, "self-test/two-mesh-scene.fbx",
                    directory / "largest-mesh.pkg")
                selected_mesh_fields = cook_one(cooker, multi_mesh_source, "self-test/two-mesh-scene.fbx",
                    directory / "selected-mesh.pkg", mesh_name="SmallTriangle")
                all_meshes_package = directory / "all-meshes.pkg"
                combined_mesh_fields = cook_one(cooker, multi_mesh_source, "self-test/two-mesh-scene.fbx",
                    all_meshes_package, all_meshes=True)
                if (int(multi_mesh_fields["triangles"]) != 2 or
                        multi_mesh_fields.get("source_mesh_count") != "1" or
                        int(selected_mesh_fields["triangles"]) != 1 or
                        selected_mesh_fields.get("source_mesh_count") != "1"):
                    raise ValueError("exact FBX mesh-name selection did not override largest-mesh selection")
                combined_subsets = base64.b64decode(combined_mesh_fields["subsets_b64"], validate=True)
                combined_indices = struct.unpack("<9I",
                    base64.b64decode(combined_mesh_fields["indices_b64"], validate=True))
                if (int(combined_mesh_fields["triangles"]) != 3 or
                        int(combined_mesh_fields["positions"]) != 7 or
                        combined_mesh_fields.get("source_mesh_count") != "2" or
                        combined_mesh_fields.get("material_slots") != "2" or
                        combined_mesh_fields.get("subset_count") != "2" or
                        struct.unpack("<6I", combined_subsets) != (0, 3, 0, 3, 6, 1) or
                        not all(index < 3 for index in combined_indices[:3]) or
                        not all(3 <= index < 7 for index in combined_indices[3:])):
                    raise ValueError("FBX all-mesh cooking did not combine source geometry and subset ranges")
                valid_package = all_meshes_package.read_text(encoding="ascii")
                for invalid_count in (0, MAX_SOURCE_MESH_COUNT + 1):
                    invalid_package = directory / f"invalid-source-mesh-count-{invalid_count}.pkg"
                    invalid_package.write_text(valid_package.replace(
                        "source_mesh_count=2", f"source_mesh_count={invalid_count}"), encoding="ascii")
                    try:
                        parse_package(invalid_package, "self-test/two-mesh-scene.fbx",
                            source_hash(multi_mesh_source))
                    except ValueError as failure:
                        if "source mesh count" not in str(failure):
                            raise
                    else:
                        raise ValueError("FBX package reader accepted an invalid source-mesh count")
                material_source = directory / "two-material-mesh.fbx"
                write_two_material_mesh(material_source)
                material_fields = cook_one(cooker, material_source, "self-test/two-material-mesh.fbx",
                    directory / "two-material-mesh.pkg")
                material_subsets = base64.b64decode(material_fields["subsets_b64"], validate=True)
                if (int(material_fields["triangles"]) != 2 or material_fields.get("material_slots") != "2" or
                        material_fields.get("subset_count") != "2" or
                        struct.unpack("<6I", material_subsets) != (0, 3, 0, 3, 3, 1)):
                    raise ValueError("FBX cooker did not preserve its two polygon material subsets")
                validate_material_package(material_fields)
                texture_directory = directory / "textured-fbx"
                texture_directory.mkdir()
                texture_source = texture_directory / "two-material-texture.fbx"
                (texture_directory / "albedo.png").write_bytes(encode_png(2, 1,
                    bytes((220, 80, 40, 255, 40, 80, 220, 255))))
                write_two_material_mesh(texture_source, "albedo.png")
                texture_fields = cook_one(cooker, texture_source,
                    "self-test/textured-fbx/two-material-texture.fbx",
                    directory / "textured-fbx-geometry.pkg")
                expected_refs = struct.pack("<10I", 1, 0, 0, 0, 0, 0, 0, 0, 0, 0)
                if (texture_fields.get("texture_source_count") != "1" or
                        base64.b64decode(texture_fields["slot_texture_sources_b64"], validate=True) != expected_refs):
                    raise ValueError("FBX cooker did not retain its base-color texture binding")
                bundled_geometry, bundled_images = package_fbx_texture_sources(
                    texture_source, (directory / "textured-fbx-geometry.pkg").read_bytes(), texture_fields)
                if (list(bundled_images) != ["fbx_image_0"] or
                        bundled_images["fbx_image_0"] != (texture_directory / "albedo.png").read_bytes() or
                        b"texture_source_" in bundled_geometry or b"texture_count=1" not in bundled_geometry or
                        base64.b64decode(dict(line.split("=", 1) for line in bundled_geometry.decode().splitlines())[
                            "slot_textures_b64"], validate=True) != expected_refs):
                    raise ValueError("FBX external texture was not converted to a packaged image slot")
                write_geometry_package(directory / "textured-fbx.elpk", bundled_geometry, bundled_images)
                ignored_texture_fields = cook_one(cooker, texture_source,
                    "self-test/textured-fbx/two-material-texture.fbx",
                    directory / "textured-fbx-ignored.pkg", ignore_material_textures=True)
                validate_material_package(ignored_texture_fields)
                if "texture_source_count" in ignored_texture_fields:
                    raise ValueError("explicitly ignored FBX textures leaked into the cooked package")


                surface_directory = directory / "surface-textured-fbx"
                surface_directory.mkdir()
                surface_source = surface_directory / "two-material-surface-texture.fbx"
                roughness_pixels = bytes((64, 1, 2, 255, 128, 3, 4, 255))
                metalness_pixels = bytes((200, 5, 6, 255, 20, 7, 8, 255))
                roughness_path = surface_directory / "roughness.png"
                metalness_path = surface_directory / "metalness.png"
                roughness_path.write_bytes(encode_png(2, 1, roughness_pixels))
                metalness_path.write_bytes(encode_png(2, 1, metalness_pixels))
                write_two_material_mesh(surface_source,
                    roughness_texture="roughness.png", metalness_texture="metalness.png")
                surface_fields = cook_one(cooker, surface_source,
                    "self-test/surface-textured-fbx/two-material-surface-texture.fbx",
                    directory / "surface-textured-fbx-geometry.pkg")
                if (surface_fields.get("texture_source_count") != "2" or
                        struct.unpack("<4I", base64.b64decode(
                            surface_fields["slot_surface_texture_sources_b64"], validate=True)) != (1, 2, 0, 0) or
                        struct.unpack("<10I", base64.b64decode(
                            surface_fields["slot_texture_sources_b64"], validate=True)) != (0,) * 10):
                    raise ValueError("FBX cooker did not preserve separate roughness and metalness source roles")
                bundled_surface_geometry, bundled_surface_images = package_fbx_texture_sources(
                    surface_source, (directory / "surface-textured-fbx-geometry.pkg").read_bytes(), surface_fields)
                runtime_surface_fields = dict(line.split("=", 1)
                    for line in bundled_surface_geometry.decode("ascii").splitlines())
                packed_image = bundled_surface_images.get("fbx_image_0")
                if packed_image is None or len(bundled_surface_images) != 1 or \
                        runtime_surface_fields.get("texture_count") != "1" or \
                        struct.unpack("<10I", base64.b64decode(
                            runtime_surface_fields["slot_textures_b64"], validate=True)) != (0, 0, 1, 0, 0, 0, 0, 0, 0, 0):
                    raise ValueError("separate FBX surface maps did not become one runtime surface slot")
                packed_width, packed_height, packed_rgba = decode_png_rgba(packed_image)
                if (packed_width, packed_height, packed_rgba) != (2, 1,
                        bytes((255, 64, 200, 255, 255, 128, 20, 255))):
                    raise ValueError("packed FBX surface image does not hold roughness in G and metalness in B")
                write_geometry_package(directory / "surface-textured-fbx.elpk",
                    bundled_surface_geometry, bundled_surface_images)
                try:
                    cook_one(cooker, material_source, "self-test/two-material-mesh.fbx",
                        directory / "under-budget.pkg", max_triangles=1)
                except subprocess.CalledProcessError as failure:
                    if "at least one triangle per FBX material subset" not in (failure.stderr or ""):
                        raise
                else:
                    raise ValueError("FBX simplification accepted fewer triangles than material subsets")
                grid_source = directory / "grid.fbx"
                cache_output = directory / "grid-cache.pkg"
                grid_output = directory / "grid.pkg"
                repeat_output = directory / "grid-repeat.pkg"
                cells_per_side = 16
                triangle_budget = 128
                write_grid_fixture(grid_source, cells_per_side)
                grid_key = "self-test/grid.fbx"
                cache_reports: list[str] = []
                cache_fields = cook_one(cooker, grid_source, grid_key, cache_output, report=cache_reports)
                grid_fields = cook_one(cooker, grid_source, grid_key, grid_output, triangle_budget)
                repeat_fields = cook_one(cooker, grid_source, grid_key, repeat_output, triangle_budget)
                original_triangles = cells_per_side * cells_per_side * 2
                if int(cache_fields["triangles"]) != original_triangles:
                    raise ValueError("cache optimization changed the triangle count")
                cache_report = re.search(r"meshoptimizer vertex cache: ACMR ([0-9.]+) -> ([0-9.]+)",
                    cache_reports[0])
                if cache_report is None or float(cache_report.group(2)) >= float(cache_report.group(1)):
                    raise ValueError("grid fixture did not improve its measured vertex-cache miss ratio")
                fetch_report = re.search(r"meshoptimizer vertex fetch: bytes fetched (\d+) -> (\d+) \(candidate (\d+)\), vertices (\d+) -> (\d+)",
                    cache_reports[0])
                if fetch_report is None or int(fetch_report.group(2)) > int(fetch_report.group(1)):
                    raise ValueError("grid fixture regressed its measured vertex-fetch cost")
                grid_triangles = int(grid_fields["triangles"])
                if not 0 < grid_triangles <= triangle_budget or grid_triangles >= original_triangles:
                    raise ValueError("grid fixture was not reduced to the requested triangle budget")
                if int(grid_fields["positions"]) >= (cells_per_side + 1) ** 2:
                    raise ValueError("simplified grid package retained unreferenced vertices")
                tangent_bytes = base64.b64decode(grid_fields["tangents_b64"], validate=True)
                for tangent in struct.iter_unpack("<4f", tangent_bytes):
                    if abs(tangent[0] - 1.0) > 0.02 or abs(tangent[1]) > 0.02 or \
                            abs(tangent[2]) > 0.02 or abs(tangent[3] - 1.0) > 0.02:
                        raise ValueError("planar UV fixture did not produce the expected +X tangent frame")
                if grid_fields != repeat_fields or grid_output.read_bytes() != repeat_output.read_bytes():
                    raise ValueError("simplified grid package output is not deterministic")
                material_grid_source = directory / "material-grid.fbx"
                write_grid_fixture(material_grid_source, 8, two_materials=True)
                material_grid_fields = cook_one(cooker, material_grid_source,
                    "self-test/material-grid.fbx", directory / "material-grid.pkg", max_triangles=64)
                material_grid_subsets = base64.b64decode(material_grid_fields["subsets_b64"], validate=True)
                subset_words = struct.unpack("<6I", material_grid_subsets)
                if (int(material_grid_fields["triangles"]) != 64 or
                        material_grid_fields.get("material_slots") != "2" or
                        material_grid_fields.get("subset_count") != "2" or
                        subset_words[0] != 0 or subset_words[2] != 0 or
                        subset_words[3] != subset_words[1] or subset_words[4] == 0 or subset_words[5] != 1 or
                        subset_words[1] + subset_words[4] != int(material_grid_fields["indices"])):
                    raise ValueError("simplification did not preserve the two material subset partitions")
                print(f"FBX cooker self-test passed: mesh selection and material subsets plus {original_triangles} -> "
                    f"{grid_triangles} deterministic simplified grid triangles; vertex-cache ACMR "
                    f"{cache_report.group(1)} -> {cache_report.group(2)}; vertex-fetch bytes "
                    f"{fetch_report.group(1)} -> {fetch_report.group(2)}")
            else:
                package_output = options.output.expanduser().resolve()
                geometry_output = directory / "geometry.pkg"
                fields = cook_one(cooker, options.source, options.asset_path, geometry_output,
                    options.max_triangles, options.mesh_name,
                    ignore_material_textures=options.ignore_material_textures,
                    all_meshes=options.all_meshes)
                if package_output.suffix.lower() == ".elpk":
                    geometry, images = package_fbx_texture_sources(
                        options.source.expanduser().resolve(strict=True), geometry_output.read_bytes(), fields)
                    write_geometry_package(package_output, geometry, images,
                        dependencies=options.dependency)
                elif "texture_source_count" in fields:
                    raise ValueError("FBX external material textures require an .elpk output bundle")
                else:
                    package_output.parent.mkdir(parents=True, exist_ok=True)
                    temporary_output = package_output.with_name(package_output.name + ".tmp")
                    try:
                        temporary_output.write_bytes(geometry_output.read_bytes())
                        os.replace(temporary_output, package_output)
                    finally:
                        temporary_output.unlink(missing_ok=True)
                print(f"FBX package validated: {fields['triangles']} triangles, {fields['positions']} vertices")
    except (OSError, RuntimeError, ValueError, subprocess.CalledProcessError) as failure:
        print(f"FBX cooking failed: {failure}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
