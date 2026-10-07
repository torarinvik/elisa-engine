# Authored animation benchmark — work in progress, 2026-10-07

## Fixture and scope

`scripts/animation_ozz_benchmark.py --authored` prepares the locally pinned
Ozz Cesium Man glTF source through `scripts/authored_animation_fixture.py`.
The authored skin has 19 joints, one two-second clip and 4,672 triangles.
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

- package: `8b97bf1b553d2b00b63f897eee7a52c36a791620b61c6a72d53d8ebc7d03fd05`
- contract: `1fc67347734e75a5fd77259de2aa441926c4432edd2f6330d44de81fd7a5f48d`

Detailed repair indices, source sums and hashes are retained in
`build/authored-animation/cesium-man.provenance.json`.

## CPU evidence and visual failure

The shared benchmark now accepts an asset/clip configuration, asserts exact
keyed library counts, shared immutable resources, changed finite sampled poses
and independent instance poses. It presents eight spatially separated instances
outside the timed region and captures the authored crowd. The guide's two skin
joints correspond to **four runtime rig nodes**; the default benchmark's count
assertion was corrected after it failed status 17.

Three optimized SDL3/Metal processes ran the authored variant on Apple M5:

| Run | p50 µs / eight updates | p95 µs | p99 µs | sampled CPU footprint peak | sampled GPU allocation peak |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 117.541 | 121.542 | 126.541 | 701,416,840 B | 423,510,016 B |
| 2 | 114.667 | 117.791 | 121.458 | 699,794,800 B | 423,510,016 B |
| 3 | 114.542 | 120.416 | 125.875 | 699,712,904 B | 423,510,016 B |

Each run records 600 samples after 300 warmup updates, with zero allocations
in 4,800 timed public animation calls. The CPU target is p99 <=1 ms per eight
updates. Rendering and memory queries are outside timing; no GPU frame-time
claim is made. Results are local scene/hardware evidence, not portable budgets.
These runs preceded the newest compiler installation; fresh provenance-aware
results remain required.

**Visual acceptance failed.** Inspection of the presented crowd found flattened
or sideways figures. The new capture check requires an upright silhouette in
each of eight viewport columns; it rejects the retained native capture at
figure 1. CPU timings alone do not satisfy this slice. The next action is to
correct skin ancestor/mesh-space conversion and verify authored deformation.
The current cooker cancels common mesh/skeleton ancestors while ignoring the
mesh-node transform. This is a suspected cause, not yet a proven repair.

The [glTF skinning specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#joint-hierarchy)
distinguishes joint hierarchy transforms from the ignored skinned mesh-node
transform. Preserve that distinction, the source inverse-bind meaning and the
engine's keyed bind invariants in the repair.

Pre-fix capture: `build/authored-animation/crowd-before.png`. CPU report:
`build/animation-authored-benchmark.json`. The default benchmark's last run
failed its old rig-count assumption; the corrected default run is pending.
The report now records compiler product/provenance and visual acceptance.

## Reproduction

```sh
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
WICKED_ROOT=../elisa-boxing-wickedengine \
ELISA_COMPILER_BIN=elisac-stage1 \
/opt/homebrew/bin/python3.14 scripts/animation_ozz_benchmark.py --authored
```

Expect visual rejection until the cooker/rig fix is verified. The full runtime
plan and authored animation acceptance remain open.
