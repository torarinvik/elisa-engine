"""Serialize production geometry loader test expectations."""

from pathlib import Path
import struct


def float32_text(value: float) -> str:
    return repr(struct.unpack("<f", struct.pack("<f", value))[0])


def manifest_line(directory: Path, verdict: str, name: str, expectation) -> str:
    """One loader-test manifest line; see native/geometry_subset_test.cpp."""
    fields = [verdict, str(directory / name)]
    if verdict == "reject":
        return "\t".join(fields + [expectation])
    index_count, slots, subsets, *records = expectation
    animation_count = None
    morph_count = None
    camera_count = None
    light_count = None
    inverse_bind_values = None
    uv1_values = None
    slot_names = None
    mesh_placements = None
    if len(records) >= 2 and records[-2] == "mesh_placements":
        mesh_placements = records[-1]
        records = records[:-2]
    if len(records) >= 2 and records[-2] == "slot_names":
        slot_names = records[-1]
        records = records[:-2]
    if len(records) >= 6 and records[-6] == "uv1":
        uv1_values = records[-5:]
        records = records[:-6]
    if len(records) >= 2 and records[-2] == "inverse_binds":
        inverse_bind_values = records[-1]
        records = records[:-2]
    while len(records) >= 2 and records[-2] in ("animations", "morphs", "cameras", "lights"):
        marker, count = records[-2:]
        if marker == "animations":
            animation_count = count
        else:
            if marker == "morphs":
                morph_count = count
            elif marker == "cameras":
                camera_count = count
            else:
                light_count = count
        records = records[:-2]
    fields += [str(index_count), str(slots)] + [str(value) for subset in subsets for value in subset]
    if records:
        fields.append("materials")
        for record in records[0]:
            record += (0,) * (17 - len(record))
            fields += [float32_text(value) for value in record[:10]] + [str(value) for value in record[10:]]
    if len(records) > 1:
        fields.append("sections")
        for name, checksum in records[1]:
            fields += [name, str(checksum)]
    if slot_names is not None:
        fields += ["slot_names"] + [name.encode("utf-8").hex() for name in slot_names]
    if animation_count is not None:
        fields += ["animations", str(animation_count)]
    if morph_count is not None:
        fields += ["morphs", str(morph_count)]
    if camera_count is not None:
        fields += ["cameras", str(camera_count)]
    if light_count is not None:
        fields += ["lights", str(light_count)]
    if inverse_bind_values is not None:
        fields += ["inverse_binds"] + [float32_text(value) for value in inverse_bind_values]
    if uv1_values is not None:
        fields += ["uv1"] + [str(value) for value in uv1_values]
    if mesh_placements is not None:
        fields += ["mesh_placements", str(len(mesh_placements))]
        for placement in mesh_placements:
            fields += [str(value) for value in placement[:8]]
            fields += [float32_text(value) for value in placement[8:]]
    return "\t".join(fields)
