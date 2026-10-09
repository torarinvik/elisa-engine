# Character Course default-grant adoption — 2026-10-09

## Compiler product used

The pulled compiler source revision is
`4655dbaa17131bcb158be69d5b4939bf64eb6bea`. At the time of these checks, its
Stage1 provenance recorded source-tree SHA256
`40b306621d1e1aa7cdd68a73179360ba1b6b9ba61aae2d491cd3087d4b819fff`, Stage1
SHA256
`8a50917d3acbf78b548cd92fc8995ef4132e69b3c792709bfdd974c814b695f8`, and
matching runtime SHA256
`013d317413defc5ffd2f79fb8dd791db6d6fd6a3217edc45fa62a81f4fc03df8`.
`stage1_provenance.py check` passed. The compiler checkout had an uncommitted
JSON standard-library grant edit, so this is source-matched candidate evidence,
not a clean promoted compiler release.

## Engine source adoption

The full Character Course project wrapper initially reported 265
`Global.Read/Write` diagnostics. The engine now scopes the affected calls in 82
functions across 23 included source/test files and in the four additional public
entry wrappers. The function-local `can Global.Read, Global.Write:` blocks
declare where these effects are used. Explicit returns preserve the original
tail and branch results, since a `can` block is a statement in Elisa. A
whitespace-insensitive diff audit found no other source-expression changes.

## Strict project checks

Each wrapper includes `src/runtime/public.elisa` and one actual project
entrypoint. All five pass `-emit check` with zero diagnostics and without
`-permissive`:

- `examples/character_course/main.elisa`
- `examples/character_course/self_test_main.elisa`
- `examples/character_course/relaunch_main.elisa`
- `examples/character_course/stream_test_main.elisa`
- `examples/character_course/live_input_test_main.elisa`

Logs are retained in the ignored `build/validation/character-course-*-newest-check.log`
files. `python3 scripts/check_source_length.py` and `git diff --check` also pass.

This closes the strict semantic grant check for the current Character Course
source. A fresh native build, self-test execution, optimized package and
relocation acceptance remain open. The shared compile lane is currently
occupied by the coordinated Studio/compiler qualification, so native relinking
is deferred until that lane is released.
