# Character course durable beacons and relaunch

Validated on 2026-09-28 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler (`ELISA_ALLOW_STALE_STAGE1=1`), SDL3/Metal, Wicked and Jolt.

This is delivery-queue item 4 (W04/W06, with the relevant parts of W03, W08
and Q06a) in the second game. It builds on
[`prefab-scene-save.md`](prefab-scene-save.md) and
[`course-checkpoints.md`](course-checkpoints.md).

## Design

- **Scene.** `examples/character_course/beacons.elisa` (`CourseBeacons`)
  authors one prefab with a post and a lamp. It places three nested scene
  links along the course. The lamp nodes carry per-instance overrides: when
  the player comes within 2.2 m, the lamp's transform is raised and its
  material is switched to the lit material.
- **Stable asset IDs.** Each visual names an `Assets::AssetId` on the
  course's own shelf (`high = 7301`, lows 1–5). A small table creates the
  render instances from those IDs: the mesh ID picks the primitive and the
  material ID picks the colour. A snapshot that names any ID outside the
  shelf is refused before any world is built.
- **Durable save.** `save` writes the `PrefabScene::SceneSnapshot` through
  `PrefabSceneCodec` into the `course-beacons` UserData payload. `load` maps
  codec failures to codes: missing, wrong version, corrupt, rejected snapshot
  or failed. It then checks the link count and the shelf.
- **Fresh-world reconstruction.** `replace` builds a new `World::World` and
  `Instance`, then runs `spawn_restored` against the authored scene. Only
  when that succeeds does it release the old render instances, destroy the
  old prefab, move the new world and instance in, and create renders from
  the restored asset IDs. A failed load leaves the old world, lamps and
  renders untouched.
- **Events.** Each lamp lit announces an `AudioCue` through a
  `WorldEvents::Queue` frame (subscribe, begin frame, Simulation, `emit_world`,
  dispatch, end frame). `emit_world` validates the lamp against the live
  world, so a lamp reference from before a load raises `InvalidEntity`.
- **Game.** In `play.inc`, walking past a beacon lights it, plays the save
  sound and shows a HUD line. Q saves the checkpoint and the beacons, L loads
  both, and R resets both. The beacons are released at exit.
- **Relaunch.** `relaunch_main.elisa` runs `run_mode(2)`, a second process
  that reads the saves the self-test left behind. The smoke list runs
  `character-course-relaunch-smoke` straight after `character-course-smoke`,
  and `--only` refuses to run it alone.

## Checks

Codes 131–147 run in the course self-test; 148–152 run in the relaunch
process.

| Code | Check |
|---|---|
| 131 | create: ready, no lamps lit, 9 live entities, 6 renders |
| 132 | load with no save reports missing and keeps the beacons |
| 133 | lighting beacons 0 and 2 announces exactly twice; relighting is a no-op |
| 134 | lit lamps are raised, unlit lamps are not |
| 135 | save succeeds |
| 136 | lighting beacon 1 after the save gives three lit |
| 137 | load restores two lit, lamp 1 unlit, restored heights |
| 138 | the pre-load lamp `EntityRef` is dead and its event raises `InvalidEntity` |
| 139 | live entities and render instances stay bounded after load |
| 140 | a corrupt payload is refused with the live world unchanged |
| 141 | a wrong-version payload is refused with the live world unchanged |
| 142 | a snapshot naming an unknown material is refused |
| 143 | a snapshot from a foreign scene is rejected, and renders stay bounded |
| 144 | three save/load cycles keep entity and render counts bounded |
| 145 | reset clears every lamp |
| 146 | hands the relaunch beacons 0 and 2 lit, plus a checkpoint |
| 147 | release returns to the render baseline |
| 189 | a shelf-known material with no shipped package fails the load (`LOAD_ASSET_FAILED`); the live beacons, render count and stream stay unchanged |
| 148 | relaunch: the checkpoint loads in the new process |
| 149 | relaunch: the character pose matches the saved position |
| 150 | relaunch: beacons 0 and 2 come back lit, 1 unlit |
| 151 | relaunch: reset and restart return to the course start |
| 152 | relaunch: renders are released, then both saves are removed |

Commands:

```
PYTHONPATH=scripts python3.14 scripts/application_native_smoke.py --only character-course-smoke,character-course-relaunch-smoke
elisascript scripts/check.elisascript
elisascript scripts/native_gate.elisascript native
```

Both smokes pass. The full check and the native gate also pass (`check=0`,
`gate=0`); they ran on the tree that also holds the next slice,
[`course-accessibility.md`](course-accessibility.md).

## Cooked beacon packages (2026-10-01)

Restored mesh and material IDs now load cooked packages. `make_cells.py`
writes `cells/beacon-post.elpk` (box), `beacon-lamp.elpk` (octahedron) and
three albedo packages (`beacon-stone`, `beacon-unlit`, `beacon-lit`).
`CourseBeacons::start` mounts `cells`. Each render row requests its mesh and
albedo through `AssetStream`, pumps to resident within a bounded number of
pumps, creates the row with `RenderScene::create_streamed_mesh`, checks that
the texture is bound, then drops both streams. `replace` builds the new rows
before it releases the old ones, so a missing package (`ABSENT_MATERIAL` is on
the shelf but not shipped) fails the whole load with nothing published.
A lit/unlit swap rebuilds only that lamp's row.

`create` no longer spawns: callers call `create`, then `start`. With the
spawn inside `create`, its ~1.9 MB frame stacked under `replace` and the
self-test overflowed the 8 MB main stack (exit 245). The same chain was
already ~8.2 MB at 0147d71c.

## Negative controls

- **Shelf check skipped in `load`.** The self-test fails at 142.
- **`replace` leaks old renders and skips the link-count check.** The
  self-test fails at 139.

Both controls were reverted.

## Compiler notes

- Passing `PrefabScene::SceneSnapshot()` as a by-value temporary into
  `replace` crashed (a null `memmove` in the callee's prologue). The
  snapshot is now a named local passed by reference.
- `for index in 0..<batch_count(beacons.renders) |beacons|:` with
  `try destroy(try batch_get(beacons.renders, index))` raised on the second
  release through a forwarded `mutable Beacons&`. Reading a local copy of the
  batch, then a plain loop with the handle bound first, works.

## Gaps

- Lamp materials are flat albedo packages; there is no authored PBR
  material beyond the base-colour map.
- There is no runtime-schema migration chain or crash-recovery journal
  through the runtime byte API (W06). A wrong version is refused, not
  migrated.
- The event path is the portable queue. Native worker-to-main events are not
  used here (W08).
- No sanitizer run of the course binary. The codec's malformed-data coverage
  stays in the portable test. Injected resource failures, resize and higher
  iteration counts for Q06a remain.
