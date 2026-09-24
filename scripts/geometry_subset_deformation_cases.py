"""Malformed compressed skin and morph package variants."""

from __future__ import annotations

from collections.abc import Callable


def cases(morph: bytes, skin_field: Callable[[str, str | None], bytes]) -> list[tuple]:
    morph_without_positions = b"\n".join(line for line in morph.splitlines()
        if not line.startswith((b"morph_0_positions_b64=", b"morph_0_positions_meshopt_b64="))) + b"\n"
    morph_corrupt_position = morph.replace(b"morph_0_positions_meshopt_b64=",
        b"morph_0_positions_meshopt_b64=AA==", 1)
    morph_corrupt_normal = morph.replace(b"morph_0_normals_meshopt_b64=",
        b"morph_0_normals_meshopt_b64=AA==", 1)
    morph_extra_target = morph + b"morph_1_positions_meshopt_b64=AA==\n"
    invalid_skin_stream = "invalid cooked geometry skin counts or influence streams"
    return [
        ("reject", "skin-corrupt-indices.pkg", skin_field("skin_indices_meshopt_b64", "AA=="),
            invalid_skin_stream),
        ("reject", "skin-corrupt-weights.pkg", skin_field("skin_weights_meshopt_b64", "AA=="),
            invalid_skin_stream),
        ("reject", "morph-missing-position.pkg", morph_without_positions, "morph position stream"),
        ("reject", "morph-corrupt-position.pkg", morph_corrupt_position, "morph position stream"),
        ("reject", "morph-corrupt-normal.pkg", morph_corrupt_normal, "morph normal stream"),
        ("reject", "morph-extra-target.pkg", morph_extra_target, "morph stream fields"),
    ]
