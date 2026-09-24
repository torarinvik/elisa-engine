"""Encode material factors stored outside the fixed-size slot record."""

from __future__ import annotations

import struct

import cook_gltf_textures

SLOT_NORMAL_SCALE_STRIDE = 4
SLOT_OCCLUSION_STRENGTH_STRIDE = 4
SLOT_CLEARCOAT_FACTOR_STRIDE = 12


def unit_factor(value, label: str) -> float:
    if type(value) not in (int, float) or not 0.0 <= value <= 1.0:
        raise ValueError(f"material {label} must be a finite number in [0, 1]")
    return float(value)


def unit_factors(value, count: int, label: str) -> list[float]:
    if not isinstance(value, list) or len(value) != count:
        raise ValueError(f"material {label} must list {count} numbers")
    return [unit_factor(component, label) for component in value]


def validate_clearcoat(document: dict, material: dict) -> None:
    extension_name = cook_gltf_textures.KHR_MATERIALS_CLEARCOAT
    extensions = material.get("extensions", {})
    if not isinstance(extensions, dict) or set(extensions) - {extension_name}:
        raise ValueError("runtime geometry cooker encountered unsupported material extensions")
    clearcoat = extensions.get(extension_name, {})
    allowed = {"clearcoatFactor", "clearcoatTexture", "clearcoatRoughnessFactor",
        "clearcoatRoughnessTexture", "clearcoatNormalTexture"}
    if not isinstance(clearcoat, dict) or set(clearcoat) - allowed:
        raise ValueError("runtime geometry cooker encountered unsupported clearcoat properties")
    if extension_name in extensions:
        used = document.get("extensionsUsed", [])
        if not isinstance(used, list) or extension_name not in used:
            raise ValueError("KHR_materials_clearcoat must be listed in extensionsUsed")
    unit_factor(clearcoat.get("clearcoatFactor", 0.0), "clearcoatFactor")
    unit_factor(clearcoat.get("clearcoatRoughnessFactor", 0.0), "clearcoatRoughnessFactor")


def factor_sidecars(slots: list[tuple]) -> tuple[bytes, bytes, bytes]:
    normal_scales = [scale for _, _, scale, *_ in slots]
    occlusion_strengths = [strength for _, _, _, strength, *_ in slots]
    clearcoat_factors = [(factor, roughness, normal_scale)
        for _, _, _, _, factor, roughness, normal_scale in slots]
    return (
        struct.pack(f"<{len(normal_scales)}f", *normal_scales)
        if any(scale != 1.0 for scale in normal_scales) else b"",
        struct.pack(f"<{len(occlusion_strengths)}f", *occlusion_strengths)
        if any(strength != 1.0 for strength in occlusion_strengths) else b"",
        b"".join(struct.pack("<3f", *factors) for factors in clearcoat_factors)
        if any(factors != (0.0, 0.0, 1.0) for factors in clearcoat_factors) else b"",
    )
