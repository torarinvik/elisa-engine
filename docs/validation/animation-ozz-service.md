# C02 ozz runtime animation service

Date: 2026-10-02; runtime integration update: 2026-10-07

## What changed

- `native/ozz_animation_service.h` promotes the ozz probe into reusable
  services. `OzzRig` holds the skeleton and the engine-to-ozz joint maps, and
  `OzzClip` holds a runtime animation. `OzzPoseContext` and `OzzContextSlot`
  own a sampling context plus SoA scratch. `sample()` writes the engine pose
  layout (10 floats per joint) into caller-owned output and does not allocate.
- `native/elisa_anim_v1.h` is a bounded native reader for the C01
  `elisa-anim-v1` contract. It checks magic, version, length, checksum,
  hashed rig identity, engine units, counts, unique joint/clip IDs, parent and
  track order, finite normalized transforms, key ticks, and ordered event
  bounds before it builds an ozz rig and clips.
- When the glTF cooker is asked to write an `.anim` sidecar, it embeds the
  same bounded contract in the `.pkg`. The geometry reader checks its format,
  declared length, and base64 bytes. The render scene then matches the keyed
  rig's joint IDs, parents, and rest transforms against the skinned mesh and
  matches every contract clip ID against a cooked clip name before binding it.
  Contract clips become the shared ozz library's source. Playback time scales
  from the `.pkg` duration to the contract duration; this preserves the game
  timeline when their durations differ by less than one sample interval.
  Packages without an embedded contract retain the fixed-rate path.
- Render scene: skinned instances now sample through ozz.
  - `native/render_scene_ozz_animation.inc` builds one immutable
    `OzzAnimationLibrary` (rig, clips, start and end poses) per cooked asset
    and caches it by asset.
  - Each `InstanceSlot` owns two context slots (current and previous clip)
    and two pose scratch buffers.
  - `apply_animation_pose` samples full poses from that scratch. Crossfades,
    phase matching, interruption, root motion and clones keep their existing
    behaviour.
- The build scripts (`elisa_build_run.py`, `render_scene_native_smoke.py`)
  add the ozz include directory and its three static libraries.

## Evidence

- `test/ozz_animation_service_test.cpp`, run by `scripts/native_unit_tests.py`:
  - `guide_rig.anim` matches a linear/nlerp reference. The worst error is
    3.8e-4; ozz stores translation and scale keys as half floats.
  - Tampered images are refused for bad checksum or rig hash, non-engine
  units, a forward parent, a truncated file, out-of-order track, duplicate
  joint/clip IDs, non-finite key, zero scale, and invalid event ID, tick or
  order.
  - 256 characters × 300 ticks: 192 guide rigs plus 64 synthetic 64-joint
    rigs with 3 clips, each with its own clip and time. The 76,800 samples
    made 0 calls to `operator new` and 0 calls to the ozz allocator.
  - Independence and determinism hold. A control confirms that context
    preparation is counted.
- `test/render_scene_animation_crowd_native.elisa`, render group 271 cases 71–77:
  - Two `guide_rig.pkg` instances assert that the renderer bound the embedded
    keyed contract, advance at independent rates, and complete 120 steady
    ticks with zero additional heap allocations. The existing eight-instance
    `lift`/`drop`/`root-motion` crowd asserts that assets without the contract
    continue to use fixed-rate fallback.
  - Eight skinned instances of two cooked assets play `lift`, `drop` and
    `root-motion` at different speeds with 0.25 s fades.
  - They run 119 steady ticks with varying per-instance deltas through the
    public `RenderScene::advance_animation`.
  - The test-probe allocation counter (armed inside
    `elisa_render_scene_v1_advance_animation`) is unchanged across those ticks,
    and the phases stay independent.
  - Mutant check: one heap allocation added to `sample_ozz_full_pose` makes
    case 75 fail.
- The existing animation native tests (submission, blend, root motion, clone)
  and the SDL3/Metal render smoke pass on ozz sampling without widening any
  tolerance.

## 2026-10-07 runtime integration evidence

`/opt/homebrew/bin/python3.14 scripts/render_scene_native_smoke.py --only native`
passes on macOS 27.0.1 / Apple M5 with Stage1 revision
`7b27fa312c5af923f044f6ee0e5e1de4f811f595`. It builds and runs the SDL3/Metal
render suite, including the two keyed course instances and the eight legacy
fixed-rate crowd instances. The resulting local executable is
`build/render-scene-native-smoke` (SHA-256
`c174c40738b1467bbf51ccc2929c29b0cdb8569543a83e67e7f2bc4b33cd1fcf`). The
embedded course rig package is SHA-256
`928306d291a467f9c650c8b9c1da0d46ad7b337413126395ff9a858ca65fd859`, and the
`.anim` sidecar is
`611ebc479c22eb270d67230329d60837f8c3233832bf78cd4004ea9f86746e19`. The
package is regenerated and checked by
`/opt/homebrew/bin/python3.14 examples/character_course/make_rigs.py --check`.

`/opt/homebrew/bin/python3.14 scripts/application_native_smoke.py --only character-course-smoke`
also passes against the same cooked course assets (status 0, 148.396 seconds).
Its structured report is `build/native-smoke/character-course-smoke.json`,
with output in `build/native-smoke/character-course-smoke.log`. That gameplay
run covers the route and lifecycle; the render crowd test separately asserts
keyed ozz source selection and allocation behavior.

## Optimized keyed clone benchmark (2026-10-07)

Run from the engine root:

```sh
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
ELISA_COMPILER_BIN=../Elisa-compiler/bin/elisac-stage1 \
/opt/homebrew/bin/python3.14 scripts/animation_ozz_benchmark.py
```

The harness builds both the Elisa archive and native bridge with
`-O2`, then runs three isolated SDL3/Metal processes on macOS 27.0.1 / Apple
M5. Each process creates eight instances from one keyed two-joint guide
package, asserts that they share one decoded package and one immutable Ozz
rig/clip library, and warms up for 300 group updates. Each of 600 measured
samples times eight public `advance_animation` calls with independent clip
speeds. The allocation probe reports zero allocations across the 4,800
measured calls in each process.

| Run | p50 µs / 8 instances | p95 µs | p99 µs | max µs | sampled CPU footprint peak | sampled GPU allocation peak |
|---|---:|---:|---:|---:|---:|---:|
| 1 | 27.750 | 31.041 | 31.209 | 33.917 | 264,602,872 B | 400,392,192 B |
| 2 | 28.291 | 29.958 | 30.083 | 30.625 | 257,951,064 B | 400,392,192 B |
| 3 | 27.000 | 27.250 | 27.416 | 35.292 | 245,302,592 B | 400,392,192 B |

The timed region covers animation API work only; it does not pump or render a
frame, so no GPU frame-time claim is made. Memory is sampled before asset
creation and every tenth measured update, outside the timed region; the table
reports the highest sampled values, not an OS high-water mark. This synthetic
two-joint fixture is a path-throughput baseline, not a production-rig target.
Raw nanosecond samples and build hashes are in
`build/animation-ozz-benchmark.json` (report SHA-256
`8b07122b05a33ec3dd83e2e238422aa98118951509209493051877f725fad241`;
executable SHA-256 `757a39ee957040a016ca7bbc0345a36be5816d8794dc46a408894f7308496847`).

## Gaps

- The keyed source is opt-in at cook time; legacy assets remain on fixed-rate
  tracks until recooked with the `.anim` sidecar option.
- First binding a clip set still prepares contexts and allocates. Steady-state
  ticks allocate nothing. The benchmark covers the synthetic two-joint guide
  rig and does not establish costs for larger authored rigs; the ozz service
  test is not in `run_boundary_sanitized.py`.
- No new Elisa source policy was added, so the proof is unchanged:
  `proof/animation_package_index.elisa` remains proved/replayed at 110/110.
