"""Cooked-package loader cases for glTF occlusion-strength sidecars."""

RECORDS = "invalid cooked slot occlusion strengths"
RANGE = "cooked slot occlusion strength is out of range"


def cases(strip_package, paint) -> list[tuple]:
    two = [(0, 3, 0), (3, 3, 1)]
    return [
        ("accept", "occlusion-strength.pkg", strip_package(2, two, 2,
            materials=[paint, paint], occlusion_strengths=[0.25, 0.75]),
            (6, 2, two, [paint, paint], "occlusion_strengths", [0.25, 0.75])),
        ("reject", "occlusion-strength-without-materials.pkg", strip_package(2, two, 2,
            occlusion_strengths=[0.25, 0.75]), RECORDS),
        ("reject", "occlusion-strength-stride.pkg", strip_package(2, [(0, 6, 0)], 1,
            materials=[paint], occlusion_strengths=[0.75], occlusion_strength_stride="8"), RECORDS),
        ("reject", "occlusion-strength-no-records.pkg", strip_package(2, [(0, 6, 0)], 1,
            materials=[paint], occlusion_strengths=[0.75], omit=("slot_occlusion_strengths_b64",)), RECORDS),
        ("reject", "occlusion-strength-count.pkg", strip_package(2, [(0, 6, 0)], 1,
            materials=[paint], occlusion_strengths=[0.25, 0.75]), RECORDS),
        ("reject", "occlusion-strength-negative.pkg", strip_package(2, [(0, 6, 0)], 1,
            materials=[paint], occlusion_strengths=[-0.01]), RANGE),
        ("reject", "occlusion-strength-overflow.pkg", strip_package(2, [(0, 6, 0)], 1,
            materials=[paint], occlusion_strengths=[1.01]), RANGE),
        ("reject", "occlusion-strength-nan.pkg", strip_package(2, [(0, 6, 0)], 1,
            materials=[paint], occlusion_strengths=[float("nan")]), RANGE),
        ("reject", "occlusion-strength-infinite.pkg", strip_package(2, [(0, 6, 0)], 1,
            materials=[paint], occlusion_strengths=[float("inf")]), RANGE),
    ]
