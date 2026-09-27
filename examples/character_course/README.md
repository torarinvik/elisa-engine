# Character course

This playable SDL3/Metal example uses Elisa's public `RuntimeServices`,
`PhysicsRuntime`, `ActionInput`, and `RenderScene` modules. Wicked/Jolt runs the
character controller and the visible course uses the same box transforms as
its collision bodies.

From the engine root, build and launch it with:

```sh
python3 scripts/elisa_build_run.py run \
  --project examples/character_course
```

Default controls are **W/S/A/D** to move (the arrow keys always move too),
**Space** to jump, hold **C** to crouch, **P** to pause/resume, **R** to restart
from the entrance and **Escape** to quit. A gamepad uses the left stick or D-pad
to move, South to jump, East to crouch, Start to pause, West to restart and Back
for the controls menu. The on-screen legend always shows the current bindings.
Losing window focus while playing pauses the game and releases held keys.

While paused, press **Tab** (or gamepad Back) to rebind the six movement keys.
Use Up/Down to pick an action and press a new key to assign it. Choosing a key
that is already used swaps the two bindings. Only W A S D E V C X Z H J, 1–3,
Space, Shift and Ctrl can be assigned; any other key (including the fixed P, R,
Q, L, Tab, Esc and arrows) is refused. Tab or Esc closes the menu and saves the bindings
(`controls.elisa`, user-data key `course-controls`). If a saved record is
invalid, the game restores the defaults and says so.

Reach the green summit marker to win; falling off the course ends the
attempt until you restart. The route has a bounded step, a low tunnel, a
20-degree ramp, a raised platform, and a right-angle corner.

Press **Q** to save a checkpoint and **L** to load it, including after quitting
and relaunching. A checkpoint stores the phase, position, crouch and attempt
count as explicit codes and whole millimetres (`progress.elisa`) in the
application's user-data directory (`ELISA_USER_DATA_DIR` overrides it). A load
validates the entire record, creates the restored character, and only then
retires the running one. A missing, unreadable or out-of-range record leaves the
current run untouched and says so in the status line.

The course plays looped music (streamed from disk), a looped wind ambience and
sounds for jumping, landing, winning, falling and saving (`sounds.elisa`). Each
sound fires at most once per simulation step, landing waits six steps before it
can repeat, and a new result sound cuts off the previous one. Pausing holds the
effects and ambience while the music continues; restart and load silence the
old scene's sounds. Without an output device the game runs on miniaudio's
silent route, and if the sounds cannot be loaded it plays silently.

The WAV files in `sounds/` were synthesized for this project by
`make_sounds.py` from sine tones and seeded noise; no third-party audio is
included. `python3 make_sounds.py --check` fails if a committed file no longer
matches the script.

Build and run the hidden check without waiting for input. It covers state
transitions, step traversal, restart, save/restart/load (including rejected
records), control rebinding and persistence, and sound events, streaming and
cleanup:

```sh
python3 scripts/elisa_build_run.py build \
  --project examples/character_course \
  --main self_test_main.elisa \
  --output build/elisa-character-course-self-test
examples/character_course/build/elisa-character-course-self-test
```
