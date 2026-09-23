"""Assertions for the FBX cooker's normalized material records."""

import base64
import struct


def validate_material_package(fields: dict[str, str]) -> None:
    records = list(struct.iter_unpack("<10f2I",
        base64.b64decode(fields["slot_materials_b64"], validate=True)))
    names_data = base64.b64decode(fields["slot_material_names_b64"], validate=True)
    names = []
    offset = 0
    for _ in range(2):
        (length,) = struct.unpack_from("<I", names_data, offset)
        offset += 4
        names.append(names_data[offset:offset + length].decode("utf-8"))
        offset += length
    expected = [
        (0.16, 0.32, 0.48, 0.75, 0.0, 0.434315, 0.3, 0.2, 0.1, 0.5, 2, 0),
        (0.7, 0.2, 0.1, 1.0, 0.0, 0.65359, 0.0, 0.0, 0.0, 0.5, 0, 0),
    ]
    if (fields.get("slot_material_stride") != "48" or names != ["First", "Second"] or
            offset != len(names_data) or len(records) != len(expected) or
            any(actual[10:] != target[10:] or
                any(abs(value - reference) > 1.0e-4 for value, reference in zip(actual[:10], target[:10]))
                for actual, target in zip(records, expected))):
        raise ValueError("FBX cooker did not preserve ordered material names and PBR factors")
