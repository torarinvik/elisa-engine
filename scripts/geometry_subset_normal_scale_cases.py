"""Cooked-package loader cases for glTF normal-map scale sidecars."""

WICKED_NORMAL_SCALE_LIMIT = 65504.0
RECORDS = "invalid cooked slot normal scales"
RANGE = "cooked slot normal scale is out of range"


def cases(strip_package, paint) -> list[tuple]:
    two = [(0, 3, 0), (3, 3, 1)]
    return [
        ("accept", "normal-scale.pkg", strip_package(2, two, 2,
            materials=[paint, paint], normal_scales=[0.75, -0.75]),
            (6, 2, two, [paint, paint], "normal_scales", [0.75, -0.75])),
        ("reject", "normal-scale-without-materials.pkg", strip_package(2, two, 2,
            normal_scales=[0.75, -0.75]), RECORDS),
        ("reject", "normal-scale-stride.pkg", strip_package(2, [(0, 6, 0)], 1,
            materials=[paint], normal_scales=[0.75], normal_scale_stride="8"), RECORDS),
        ("reject", "normal-scale-no-records.pkg", strip_package(2, [(0, 6, 0)], 1,
            materials=[paint], normal_scales=[0.75], omit=("slot_normal_scales_b64",)), RECORDS),
        ("reject", "normal-scale-count.pkg", strip_package(2, [(0, 6, 0)], 1,
            materials=[paint], normal_scales=[0.75, 1.0]), RECORDS),
        ("reject", "normal-scale-overflow.pkg", strip_package(2, [(0, 6, 0)], 1,
            materials=[paint], normal_scales=[WICKED_NORMAL_SCALE_LIMIT + 1.0]), RANGE),
        ("reject", "normal-scale-nan.pkg", strip_package(2, [(0, 6, 0)], 1,
            materials=[paint], normal_scales=[float("nan")]), RANGE),
        ("reject", "normal-scale-infinite.pkg", strip_package(2, [(0, 6, 0)], 1,
            materials=[paint], normal_scales=[float("inf")]), RANGE),
    ]
