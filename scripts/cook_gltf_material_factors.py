"""Encode material factors stored outside the fixed-size slot record."""

from __future__ import annotations

import struct

SLOT_NORMAL_SCALE_STRIDE = 4
SLOT_OCCLUSION_STRENGTH_STRIDE = 4


def factor_sidecars(slots: list[tuple[bytes, list, float, float]]) -> tuple[bytes, bytes]:
    normal_scales = [scale for _, _, scale, _ in slots]
    occlusion_strengths = [strength for _, _, _, strength in slots]
    return (
        struct.pack(f"<{len(normal_scales)}f", *normal_scales)
        if any(scale != 1.0 for scale in normal_scales) else b"",
        struct.pack(f"<{len(occlusion_strengths)}f", *occlusion_strengths)
        if any(strength != 1.0 for strength in occlusion_strengths) else b"",
    )
