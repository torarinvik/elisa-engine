# Character Course state policy

`examples/character_course/state.elisa` now owns the course's phase and attempt
state. `State` keeps both fields private; gameplay reads the typed `Phase`
through accessors and changes state through restart, pause, outcome, and
validated restore operations. The spatial checks stay in the game client and
pass two booleans to the pure policy: whether the character fell and whether
it is inside the goal. Failure takes precedence if both are true. Restart
saturates at the same attempt limit used by checkpoint decoding.

The explicit phase tags live in `CourseState` and `CourseProgress` aliases
them, keeping the serialized checkpoint representation stable without
duplicated numeric values. A zero-attempt checkpoint is rejected during
decoding, before the game can apply it.

## Validation

- `../elisa-proof/build/elisa-proof --json proof/character_course_state.elisa`
  passed: 34 of 34 proof obligations, with no open obligations or findings.
- `scripts/check_module_hygiene.py`, `scripts/check_source_length.py`, and
  `git diff --check` passed.
- The hidden `character-course-smoke` passed on macOS 27.0 with Stage1
  `7b27fa312c5af923f044f6ee0e5e1de4f811f595`. The run set
  `ELISA_AUDIO_FORCE_DEVICE_UNAVAILABLE=1`; its lifecycle trace recorded zero
  active voices and streams throughout the stress iterations. The report is
  `build/native-smoke/character-course-smoke.json` and the retained log is
  `build/native-smoke/character-course-smoke.log`.

```sh
ELISA_AUDIO_FORCE_DEVICE_UNAVAILABLE=1 \
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
ELISA_COMPILER_BIN=/Users/torarinvikbjarko/.elisac/elisac-stage1 \
PYTHON_BIN=/opt/homebrew/bin/python3.14 \
/opt/homebrew/bin/python3.14 scripts/application_native_smoke.py \
  --only character-course-smoke
```

Manual visible traversal and win/fall presentation remain open under Q07a.
