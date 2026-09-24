"""Focused glTF KHR_materials_clearcoat cooker tests."""

from __future__ import annotations

from copy import deepcopy
import struct

import cook_gltf_geometry
import cook_gltf_textures


EXTENSION = cook_gltf_textures.KHR_MATERIALS_CLEARCOAT


def clearcoat_material(document: dict) -> None:
    document["materials"][1]["extensions"] = {EXTENSION: {
        "clearcoatFactor": 0.8, "clearcoatRoughnessFactor": 0.35,
        "clearcoatTexture": {"index": 2}, "clearcoatRoughnessTexture": {"index": 2},
        "clearcoatNormalTexture": {"index": 1, "scale": 0.6}}}
    document["extensionsUsed"] = [EXTENSION]


def _case(document: dict, buffer: bytes, mutate, expected: str) -> str | None:
    variant = deepcopy(document)
    mutate(variant)
    try:
        cook_gltf_geometry.normalized_geometry(variant, buffer)
    except ValueError as error:
        return None if expected in str(error) else f"clearcoat mutation failed for {error}, expected {expected}"
    return "clearcoat cooker accepted an unsupported or invalid mutation"


def check(document: dict, buffer: bytes) -> str | None:
    variant = deepcopy(document)
    clearcoat_material(variant)
    geometry = cook_gltf_geometry.normalized_geometry(variant, buffer)
    expected_references = struct.pack("<32I", 4, 0, 0, 0, 0, 0, 0, 0,
        1, 2, 3, 0, 3, 3, 3, 2,
        0, 0, 0, 1, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0)
    expected_factors = struct.pack("<12f", 0.0, 0.0, 1.0, 0.8, 0.35, 0.6,
        0.0, 0.0, 1.0, 0.0, 0.0, 1.0)
    if geometry["slot_textures"] != expected_references or geometry["slot_clearcoat_factors"] != expected_factors:
        return "glTF cooker did not preserve clearcoat maps and factors per slot"

    def factor_out_of_range(document: dict) -> None:
        clearcoat_material(document)
        document["materials"][1]["extensions"][EXTENSION]["clearcoatFactor"] = 1.01

    def roughness_out_of_range(document: dict) -> None:
        clearcoat_material(document)
        document["materials"][1]["extensions"][EXTENSION]["clearcoatRoughnessFactor"] = -0.01

    def transform(document: dict) -> None:
        clearcoat_material(document)
        document["materials"][1]["extensions"][EXTENSION]["clearcoatTexture"]["extensions"] = {
            "KHR_texture_transform": {}}

    def excessive_normal_scale(document: dict) -> None:
        clearcoat_material(document)
        document["materials"][1]["extensions"][EXTENSION]["clearcoatNormalTexture"]["scale"] = 65505.0

    def unlisted(document: dict) -> None:
        clearcoat_material(document)
        document.pop("extensionsUsed")

    for mutate, expected in (
            (factor_out_of_range, "clearcoatFactor must be a finite number in [0, 1]"),
            (roughness_out_of_range, "clearcoatRoughnessFactor must be a finite number in [0, 1]"),
            (transform, "KHR_materials_clearcoat.clearcoatTexture has unsupported properties"),
            (excessive_normal_scale, "must fit Wicked's finite half-float range"),
            (unlisted, "must be listed in extensionsUsed")):
        failure = _case(document, buffer, mutate, expected)
        if failure is not None:
            return failure
    return None
