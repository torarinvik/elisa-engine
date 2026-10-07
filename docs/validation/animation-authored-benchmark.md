# Authored animation benchmark — local acceptance passed, 2026-10-07

## Fixture and scope

`scripts/animation_ozz_benchmark.py --authored` prepares the locally pinned
Ozz Cesium Man glTF source through `scripts/authored_animation_fixture.py`.
The authored skin has 19 joints, one two-second clip and 4,672 triangles.
The cooked runtime rig has 20 nodes, including the shared orientation ancestor.
Source SHA-256:
`e97bf032c41e1199772051f591b0a16cd1de046a1c9da119ceb13538a057c0fb`.

Attribution: Cesium Man, donated by Cesium to Khronos glTF Sample Models,
CC BY 4.0; local evidence is
`dependencies/ozz/media/gltf/khronos/README.md`. The benchmark derivative
normalizes 210 vertex weight sums and replaces the texture with a neutral
material. It preserves geometry/hierarchy/motion bytes outside those recorded
weight ranges. The original engine weight rejection remains a negative
control. The fixture's original nearest-minification sampler remains unsupported;
this benchmark does not establish original textured-model import acceptance.

The normal geometry cooker generates the embedded keyed contract and sidecar.
Repeated preparation produced identical hashes:

- package: `4df398b00e30a833a08f5fb9342f0ee8c5b46c47e1b5bcd3e022fac4c3d31b8f`
- contract: `2d4a87f54560123899311ca1ae3a98372bdbd407eced0922db2316d2196bcf71`

Detailed repair indices, source sums and hashes are retained in
`build/authored-animation/cesium-man.provenance.json`.

## Optimized CPU and rendered acceptance

The shared benchmark now accepts an asset/clip configuration, asserts exact
keyed library counts, shared immutable resources, changed finite sampled poses
and independent instance poses. It presents eight spatially separated instances
outside the timed region and captures the authored crowd. The guide's two skin
joints correspond to **four runtime rig nodes**; the default benchmark's count
assertion was corrected after it failed status 17.

Three optimized SDL3/Metal processes ran the authored variant on Apple M5:

| Run | p50 µs / eight updates | p95 µs | p99 µs | sampled CPU footprint peak | sampled GPU allocation peak |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 128.334 | 133.292 | 139.375 | 692,635,016 B | 423,591,936 B |
| 2 | 127.667 | 130.458 | 136.500 | 699,876,744 B | 423,510,016 B |
| 3 | 127.917 | 131.167 | 136.625 | 693,536,160 B | 423,510,016 B |

Each run records 600 samples after 300 warmup updates, with zero allocations
in 4,800 timed public animation calls. The CPU target is p99 <=1 ms per eight
updates. Rendering and memory queries are outside timing; no GPU frame-time
claim is made. Results are local scene/hardware evidence, not portable budgets.
These runs use installed compiler `96761822`, product SHA-256
`bf41891a721b53f838bd1964cf7323de6d38e8fcb9389f05749c64c3629eae55`.
The report records its complete provenance.

**Rendered acceptance passed.** The native capture shows eight upright figures
with independently sampled poses; every viewport column passes the silhouette
check. Before repair that same check rejected figure 1. The cooker now retains
global joint ancestors and factors a common rest bind shape into vertex streams
while adjusting inverse binds: `G(t) I B^-1 (B v) = G(t) I v`.
Normals, tangents, triangle winding and morph streams use the existing placement
transform pipeline. Separate skins retain their own bind shapes; animated
non-joint ancestors remain in each rig branch. Nonuniform rest palettes retain
authored inverse binds, with the existing keyed contract rest-bind rejection.

`scripts/authored_skin_space_check.py` compares all 3,273 source vertices at rest.
The pre-fix maximum error was 2.130583 m, RMS 1.656895 m; after repair these are
1.37e-15 m and 5.87e-16 m. Both bounds now retain the 1.506550 m Y extent.
All 19 source rest palettes share their bind shape within 5.81e-7 per component.
The rest comparison does not independently prove the entire authored clip;
rendered pose-change and instance-independence checks provide runtime evidence.
Report: `build/authored-animation/cesium-man.skin-space.json`.

The glTF skin, morph, hierarchy, scene and joint-limit gates pass. Coverage
includes explicit global hierarchy/bind matrices, shared-ancestor vertex
translation with unchanged directions/morph deltas, independent roots,
multiple skins, mixed static/skinned placements and ignored mesh-only transforms.
Source-length/module-hygiene policies and diff whitespace pass.

Implementation-linked package bounds remain covered by
`proof/animation_package_index.elisa` (110/110 obligations in the recorded
Ozz-service proof run; unchanged and not replayed for this Python cooker change).
That proof does not establish floating-point bind-shape matrix algebra or GPU
deformation; the numerical and native gates above supply evidence for this
fixture, with general floating-point formal verification still unproved.

The [glTF skinning specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#joint-hierarchy)
distinguishes joint hierarchy transforms from the ignored skinned mesh-node
transform. Preserve that distinction, the source inverse-bind meaning and the
engine's keyed bind invariants in the repair.

Pre-fix capture: `build/authored-animation/crowd-before.png`. Passing capture:
`build/authored-animation/crowd.png`. CPU report:
`build/animation-authored-benchmark.json`; full log:
`build/validation/animation-authored-repair.log`. The corrected default benchmark
also passed after the cooker repair with the new compiler: worst p99
35.000 microseconds per eight updates, zero steady allocations. Log:
`build/validation/animation-default-after-skin-repair.log`.

## Reproduction

```sh
/opt/homebrew/bin/python3.14 scripts/authored_animation_fixture.py
/opt/homebrew/bin/python3.14 scripts/authored_skin_space_check.py

DEVELOPER_DIR=/Library/Developer/CommandLineTools \
WICKED_ROOT=../elisa-boxing-wickedengine \
ELISA_COMPILER_BIN=elisac-stage1 \
/opt/homebrew/bin/python3.14 scripts/animation_ozz_benchmark.py --authored
```

Local authored-rig acceptance is complete. The full implementation backlog,
GPU frame timing and portable production budgets remain open.
