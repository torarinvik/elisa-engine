# Character course whole-World save (W06)

Validated on 2026-10-01 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler (`ELISA_ALLOW_STALE_STAGE1=1`), SDL3/Metal, Wicked and Jolt.

The character course is the first shipped game to save and load a whole
`World` through `World::SaveGame` and `WorldSaveLoad::load`
([`world-save.md`](world-save.md)).

## Design

- **Props.** `examples/character_course/props.elisa` (`CourseProps`) keeps three
  crates in their own `World`. Each crate is a `Structure` entity with a
  `WorldRendering` row (render IDs 900–902) and a dynamic Jolt box body bound
  through `WorldPhysics`. `create` binds them with the same
  `WorldSaveRendering`/`WorldSavePhysics` passes that a load uses. The boxes on
  screen are created from the extracted render rows, and `sync` pulls the body
  poses back into the World every frame.
- **Save.** `save` encodes the World with `World::SaveGame::save_world` into a
  464-byte buffer and writes it as the `course-props-world` UserData payload
  (envelope version 1).
- **Load.** `load` reads the payload and hands the bytes to
  `WorldSaveLoad::load`. That call swaps the World in place, tears down the old
  bodies and rows, and rebuilds both. Only after it succeeds are the on-screen
  boxes recreated from the new rows. A rejected payload returns `LOAD_REJECTED`
  and leaves the World, rows, bodies and boxes untouched.
- **Game.** `play_loop.inc` creates the props. The player can push them, and Q/L
  save and load them together with the checkpoint and beacons.

## Checks

Codes 218–226 run in the course self-test; 227–229 run in the relaunch
process.

| Code | Check |
|---|---|
| 218 | create: 3 live crates, 3 render rows, 3 bodies, 3 boxes |
| 219 | load with no save reports missing; counts unchanged |
| 220 | the crates fall under Jolt and settle; save succeeds; crate 0 is displaced in the World |
| 221 | a payload with one flipped byte (valid UserData envelope) is rejected; the same crate reference stays live at its displaced pose |
| 222 | a 16-byte garbage payload is rejected with the live props unchanged |
| 223 | the good payload is written back |
| 224 | load: the old reference is dead; World pose, render row (ID 900+i) and body pose of every crate match the saved poses |
| 225 | three save/load cycles keep live entities, rows, bodies and boxes bounded |
| 226 | handoff poses saved for the relaunch; release returns to the render baseline with no bodies |
| 227 | relaunch: the saved World loads in a new process |
| 228 | relaunch: World, render rows and bodies sit at the handoff poses |
| 229 | relaunch: release returns to the render baseline; the save is removed |

Commands:

```
PYTHONPATH=scripts python3.14 scripts/application_native_smoke.py --only character-course-smoke,character-course-relaunch-smoke
elisascript scripts/check.elisascript
```

Gate results (2026-10-01): with the change, `scripts/check.elisascript` failed silently in six runs while the machine was heavily loaded (load average 10–20, with another gate running at the same time). Each failure was in an unrelated stage: physics_policy (rc=1), asset_lod and world_events (rc=126, the child process failed to start), and one Elisa Proof step that used a mismatched prover. Each failing stage compiled and ran cleanly on its own; physics_policy did so 6 times in a row. Run back to back in an isolated worktree with the `elisa-engine-proof` prover, at lower load, the gate passed both with the change and on HEAD (rc=0 for each). A logging compiler wrapper showed non-zero compiler exits only for the expected negative ownership tests.
