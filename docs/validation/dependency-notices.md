# Dependency notice inventory update — 2026-10-04

The Wicked build includes Jolt as `libJolt.a`. Vendored Jolt source headers identify
Jorrit Rouwe and `SPDX-License-Identifier: MIT`, but the game's generated notice
catalog did not include Jolt's license text. The catalog now pins the upstream
`LICENSE` at commit `5830c342b90fa087f118aa0f086d541a4950b1d9` and verifies its
SHA-256 (`800abe35d64ad9defd636ff1ee8c961e06f0ebca3ef8d10083e8aa0e8ef86ac3`).
The [upstream license](https://github.com/jrouwe/JoltPhysics/blob/5830c342b90fa087f118aa0f086d541a4950b1d9/LICENSE)
is collected as `Jolt.txt` in both game notice directories.

`native/notice-sources.json` records Jolt as a statically linked component, and
`scripts/check_bundled_notices.py` now verifies declared static-component notices
alongside the transitive dynamic libraries. Both project manifests ship
`third_party/notices/Jolt.txt`. A repackaged optimized Character Course app contains
50 notice text files plus the source catalog. Its additional Noto font notice is
listed in `examples/character_course/notice-sources.json`, pinned to the Google
Fonts OFL at commit `a559a6efcfed22bf50219f52ecefcf20b9522408`, and combined with
the shared catalog by the collector. All nine dynamic-library mappings and the
declared Jolt mapping verify. The overall catalog correctly remains incomplete.

## Validation

- `python3 scripts/collect_dependency_notices.py --manifest native/notice-sources.json --extra-manifest examples/character_course/notice-sources.json --output /private/tmp/elisa-course-notices-clean-20261004 --wicked-root ../elisa-boxing-wickedengine` collected 50 hash-verified sources into a fresh directory; all 51 project-declared notice paths were present.
- `python3 -m unittest discover -s scripts -p test_collect_dependency_notices.py` passed 5 tests, including merged project catalogs and hash-drift rejection.
- `python3 -m unittest discover -s scripts -p test_bundled_notices.py` passed 1 test, including missing and modified static notice rejection.
- `python3 scripts/test_package_macos_app.py` passed 28 tests.
- `scripts/check_bundled_notices.py` reported `dylib_notice_files_verified=true` for all nine libraries and `statically_linked_notice_files_verified=true` for Jolt in `/private/tmp/CharacterCourseNoticesRepro-2026-10-04.app`; its exit status remains 1 because `catalog_complete=false`.
- `python3 scripts/check_source_length.py` and `git diff --check` passed.
- Proof: not applicable; this slice updates distribution metadata and a Python file-hash audit, with no Elisa policy function. Remaining unproved: the full set of embedded/vendor notices and distribution obligations.

Next: audit the remaining vendored source sets and map each shipped static component
to its complete notice. Do not mark the catalog complete until that review is finished.
