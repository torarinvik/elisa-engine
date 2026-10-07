# Character course controls

Queue item 5 asks for saved rebinding, reliable focus and context changes,
readable resize behaviour and keyboard/controller menu operation in the running
client. This slice adds them to `examples/character_course` using only public
`ActionInput`, `ActionInputRuntime`, `Save`, `UserData` and `RenderScene` APIs.
No engine or native changes were needed.

## Design

`examples/character_course/controls.elisa` (`CourseControls`) owns:

- **Keymap:** six rebindable slots (forward, backward, left, right, jump,
  crouch). The defaults are W S A D Space C. Only 19 keys can be assigned:
  W A S D E V C X Z H J, 1–3, Space, both Shift keys and both Ctrl keys. A
  keymap is valid only when all six keys are assignable and distinct.
- **Assign:** refuses a key outside the pool (`Reserved`) and swaps two slots
  when the new key is already bound (`Swapped`). The result is always a valid
  keymap.
- **Fixed bindings:** arrows always move. Esc quits, P pauses, R restarts,
  Q saves, L loads and Tab opens the controls menu. The gamepad has left stick
  (dead zone 0.35) and D-pad movement, South jump, East crouch, Start pause,
  West restart and Back for controls.
- **Record:** user-data key `course-controls` holds version 1 and six key
  codes. `decode` returns `Rejected` with the defaults for a wrong field
  count, a wrong version, a duplicate key or a key outside the pool. `load`
  maps a missing key to `Missing` and other read failures to `Unreadable`.
- **Menu:** a pure state machine. Up/Down (or D-pad) selects a slot and wraps
  around. Tab or Esc (or gamepad East, Back or Start) closes it. Any other key
  assigns, and an unassignable key reports `Reserved`.

`play_loop.inc` holds the interactive loop; `play.inc` holds the menu and HUD helpers:

- On startup it loads saved controls. An invalid record restores the defaults
  and the status line says so.
- An on-screen legend lists every slot with its current key. The selected row
  is yellow while the menu is open.
- Tab (or gamepad Back) while paused opens the menu. While the menu is open,
  events go only to the menu and gameplay input is not updated. On close the
  game rebuilds the action map from the keymap, so no key that was held during
  the menu leaks into play. It then saves and reports whether the save worked.
- Losing focus while playing pauses the game. `ActionInputRuntime` releases held
  actions on focus loss.
- Resize relayout runs every frame, in every state, including the menu.

## Self-test (`character-course-smoke`)

`controls_test` runs after the progress test in the finite self-test:

| Code | Check |
| --- | --- |
| 70 | the default keymap is valid |
| 71 | Esc and ArrowUp are refused and leave the keymap unchanged |
| 72 | binding jump to W swaps forward to Space |
| 73 | encode/decode round-trips the swapped keymap |
| 74 | a duplicate key or an Esc key in a record is `Rejected` with defaults |
| 75 | version 2 is `Rejected` with defaults |
| 76 | on disk: missing → defaults; saved → identical; two-field record → `Rejected` with defaults |
| 77 | menu: Down selects backward, Up twice wraps to crouch, J assigns, P is `Reserved`, Esc closes and keeps J |
| 78 | a map rebuilt with forward = J ignores W, moves on J, releases on J up |
| 79 | ArrowUp still moves, and a FocusLost event releases it |
| 80 | stick 0.2 is inside the dead zone, 0.8 moves, South jumps |

Gamepad cases (80 and the menu's D-pad paths) use synthetic `ActionInput`
events. They are **mapping-only**: no physical controller was connected.

## Results (2026-09-27, Apple Silicon, macOS)

Compiler: stage1 snapshot sha256 `7ccb9831…9ce3`, built from a dirty
`b841e64b` checkout. It was pinned with `ELISA_STAGE1_BIN`
(see `native-gate.md`).

| Case | Result |
| --- | --- |
| Self-test with a scratch `ELISA_USER_DATA_DIR` | exit 0; no files left behind |
| Negative control (last check inverted) | exit 80, so the controls chain runs |
| `application_native_smoke.py --only character-course-smoke` | pass |
| Interactive `main.elisa` build, launched for 6 s and then killed | ran until the timeout signal; no startup failure |

## Gaps

- **Physical controller:** not tested. Per-controller bindings are not
  implemented (all gamepads share one logical device).
- **Focus/resize:** the new visible keyboard check did not establish focus-loss
  pause or a user-driven live resize; those still need an OS window-switch and
  resize check.
- **Pointer input:** there is no mouse movement or scroll input. The legend
  uses logical positions, which the canvas scales by the display DPI (see
  [`overlay-dpi-scaling.md`](overlay-dpi-scaling.md)).
- **Key coverage:** only the portable key subset (codes 2001–2029) exists.
  Rebinding to keys outside it needs wider `Application` key codes.
- **Gamepad rebinding:** gamepad bindings are fixed. Only keyboard slots can
  be rebound.

## Manual keyboard review (2026-10-07)

The current-source optimized app at `build/CharacterCourse-Q02-2026-10-07.app`
(build identity `7e2702b4a9b956e4`) was opened visibly. P showed the paused
controls, Tab opened the rebind menu, and the selected Forward row changed from
W to H with a visible legend update. Rebinding it to W restored the default;
closing the menu displayed `Controls saved`. A fresh app launch displayed W,
confirming the saved profile reloads. R restarted from the paused state into
the playing view, and Escape exited with `process_exit_status=0`.

The retained launcher log is
`build/validation/character-course-q02-controls-2026-10-07.log` (SHA-256
`9bbc4abfe8e96cf5118375ee4a023870a09a854b9bfebea17249be754b2c8f1c`). It
records build identity and clean exit; the on-screen state changes were
observed live and are described above. Physical controller behavior, focus
loss, live window resize, held crouch, and full traversal remain unverified.
