"""Cooked-package loader cases for glTF clearcoat factor sidecars."""

RECORDS = "invalid cooked slot clearcoat factors"
RANGE = "cooked slot clearcoat factors are out of range"


def cases(strip_package, paint) -> list[tuple]:
    one = [(0, 6, 0)]
    valid = [(0.8, 0.35, 0.6)]
    return [
        ("accept", "clearcoat-factors.pkg", strip_package(2, one, 1,
            materials=[paint], clearcoat_factors=valid),
            (6, 1, one, [paint], "clearcoat_factors", valid)),
        ("reject", "clearcoat-factors-without-materials.pkg", strip_package(2, one, 1,
            clearcoat_factors=valid), RECORDS),
        ("reject", "clearcoat-factors-stride.pkg", strip_package(2, one, 1,
            materials=[paint], clearcoat_factors=valid, clearcoat_factor_stride="4"), RECORDS),
        ("reject", "clearcoat-factors-missing-data.pkg", strip_package(2, one, 1,
            materials=[paint], clearcoat_factors=valid, omit=("slot_clearcoat_factors_b64",)), RECORDS),
        ("reject", "clearcoat-factors-count.pkg", strip_package(2, one, 1,
            materials=[paint], clearcoat_factors=[*valid, *valid]), RECORDS),
        ("reject", "clearcoat-factor-negative.pkg", strip_package(2, one, 1,
            materials=[paint], clearcoat_factors=[(-0.01, 0.0, 1.0)]), RANGE),
        ("reject", "clearcoat-roughness-overflow.pkg", strip_package(2, one, 1,
            materials=[paint], clearcoat_factors=[(0.5, 1.01, 1.0)]), RANGE),
        ("reject", "clearcoat-normal-scale-overflow.pkg", strip_package(2, one, 1,
            materials=[paint], clearcoat_factors=[(0.5, 0.5, 65505.0)]), RANGE),
        ("reject", "clearcoat-normal-scale-nan.pkg", strip_package(2, one, 1,
            materials=[paint], clearcoat_factors=[(0.5, 0.5, float("nan"))]), RANGE),
    ]
