#!/usr/bin/env python3
"""Validate cooked FBX records and bundle their external material images."""

from __future__ import annotations

import base64
import binascii
import math
from pathlib import Path, PurePosixPath
import struct

import cook_gltf_animation
from elisa_package import encoded_image_dimensions
from fbx_surface_texture import infer_alpha_mode, pack_surface_maps

MAX_PACKAGE_BYTES = 64 * 1024 * 1024
MAX_LINE_BYTES = 16 * 1024 * 1024
MAX_MATERIAL_SLOTS = 16
MAX_SOURCE_MESH_COUNT = 1024


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

    material_records = bytearray(base64.b64decode(fields["slot_materials_b64"], validate=True))
    if len(material_records) != material_slots * 48:
        raise ValueError("FBX alpha policy does not match its material slots")
    for slot in range(material_slots):
        base_color_reference = direct_references[slot * 5]
        if not base_color_reference:
            continue
        mode_offset = slot * 48 + 40
        (current_mode,) = struct.unpack_from("<I", material_records, mode_offset)
        inferred_mode = infer_alpha_mode(source_images[base_color_reference - 1], current_mode)
        if inferred_mode != current_mode:
            struct.pack_into("<I", material_records, mode_offset, inferred_mode)

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
        "slot_materials_b64": base64.b64encode(material_records).decode("ascii"),
        "texture_count": str(len(images)),
        "texture_names_b64": base64.b64encode(texture_names).decode("ascii"),
        "slot_texture_stride": "20",
        "slot_textures_b64": base64.b64encode(references).decode("ascii"),
    }
    source_fields = {"texture_source_count", "texture_source_names_b64", "slot_texture_sources_b64",
        "slot_surface_texture_sources_b64", "slot_materials_b64"}
    lines = [line for line in geometry_package.decode("ascii").splitlines()
        if line.partition("=")[0] not in source_fields]
    lines.extend(f"{key}={value}" for key, value in runtime_fields.items())
    return ("\n".join(lines) + "\n").encode("ascii"), images
