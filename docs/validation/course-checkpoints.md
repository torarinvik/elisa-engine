# Character course checkpoints

Queue item 4 asks for save, quit, load and restart in a real game, where
malformed or failed loads never publish partial state. This slice gives
`examples/character_course` a durable checkpoint on the existing `Save` and
`UserData` modules. It does not yet connect the durable scene-file reader to
fresh-world reconstruction (see Gaps).

## Record

`examples/character_course/progress.elisa` (`CourseProgress`) stores one
six-field `Save::Blob` under the user-data key `course-checkpoint`:

| Field | Meaning | Accepted on load |
| --- | --- | --- |
| 0 | phase code (1 playing, 2 paused, 3 won, 4 failed) | 1–4 |
| 1–3 | character position in whole millimetres | x ±16 m, y −8…16 m, z ±8 m |
| 4 | crouched flag | 0 or 1 |
| 5 | attempt counter | 1…1,000,000 |

Fields are explicit codes, not enum ordinals or native handles. `decode`
validates the exact field count and every field before it returns `Loaded`;
anything else is `Rejected`. `load` maps a missing key to `Missing` and any
other read failure to `Unreadable`. `encode` does not validate, so tests can
persist records a live game could never produce.

## Game behaviour

- **Q** saves the current phase, position, crouch state and attempt count. The
  status line reports success or failure.
- **L** loads. On `Loaded`, the game creates the restored character, then
  destroys the old one, then applies the phase, attempts, crouch and clock. On
  `Missing`, `Unreadable` or `Rejected` it keeps the current run and says why.
- **R** (restart) increments the attempt counter, which is part of the saved
  state.

## Self-test (`character-course-smoke`)

`self_test.inc` adds `progress_test` to the finite self-test. Each failure has
its own exit code:

| Code | Check |
| --- | --- |
| 61 | a paused, attempt-2 checkpoint survives save → restart → load unchanged |
| 62 | applying it moves the live character to the saved position |
| 63 | an out-of-range phase (9) is `Rejected` |
| 64 | a two-field blob is `Rejected` |
| 65 | after removal the load is `Missing` |
| 66 | the character position is unchanged after the rejected loads |
| 67 | three save/restart/load cycles round-trip |

The test removes the key when it finishes.

## Results (2026-09-27, Apple Silicon, macOS, compiler `b841e64b`)

| Case | Result |
| --- | --- |
| Self-test with a scratch `ELISA_USER_DATA_DIR` | exit 0; no files left behind |
| Self-test with `ELISA_USER_DATA_DIR` under a regular file | exit 3 (visible failure, no silent pass) |
| `application_native_smoke.py --only character-course-smoke` | pass |

## Gaps

- **Scene state:** authored overrides and the durable scene-file reader are not
  reconstructed into a fresh world (W04). The checkpoint covers gameplay state
  only.
- **Native resources:** only the character is rebuilt. There is no general
  rehydration of native resources from stable asset IDs.
- **Crash recovery:** there is no transactional journal or migration chain for
  this record (W06). A version change is simply rejected.
- **Relaunch:** persistence is proven through on-disk user data within one
  process. A save in one process and a load in a separate launch is not
  automated.
- **Stress:** repeated-cycle resource bounds and injected failures belong to
  Q06a.
- **Manual play:** a person has not yet checked Q/L in the interactive build.
