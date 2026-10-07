# Updated runtime package — 2026-10-07

The ordinary Character Course `main.elisa` was built at clean engine revision
`6b506a746bd89dcbba79a840988d2b7202b4c92f` after the contact/audio workload and
authored skin-space changes. Wicked revision `fd790f55` was also clean.
Production test probes were disabled. Build identity: `143a7c6d737c219b`.

The actual commands use **Elisa `-O2` and native C++ `-O2`**. The hosted archive
flag forwarding was fixed in this revision; see [optimization evidence](hosted-build-optimization.md).
Elisa archive compilation took 71.97 s, native linking 30.80 s, packaging 1.29 s.
Installed compiler source revision is `96761822`, product SHA-256
`bf41891a721b53f838bd1964cf7323de6d38e8fcb9389f05749c64c3629eae55`.
Runtime object SHA-256:
`02d868eb68739e517830684eb89bb820bec16f8d07f92ff7bbacee35ef188cb6`.

Pre-package executable SHA-256:
`fba0eb9e8ab3a7f2eee83cbb504f6df6c0d94567c1de6d6a606ac0fc8adb632f`.
The packaged binary, after load-command changes and ad-hoc signing, has SHA-256
`4dd6593fb8c890275f647c4867f5c62eb6bcea1fd861d6d861f1cf70202eaeae`.
It matches the bundle's `build-provenance.json`. All 37 declared resource
files match source SHA-256 values: 20 cells, one font, two rigs, 13 sounds,
one text resource. Hash report:
`build/validation/character-course-runtime-slices-package-hashes.json`.

`build/CharacterCourse-RuntimeSlices-2026-10-07.app` passed the existing
interactive relocated validator on macOS 27.0.1/arm64. It copied the app to a
temporary path containing spaces, denied the entire project-checkout parent
and `/opt/homebrew`, checked that both deny controls failed, and blocked
outbound network. Metal, Wicked and Jolt startup markers appeared in 1.221 s.
SIGTERM entered the application quit loop; launcher status was 0 and the whole
process group was reaped. The five resource groups were present.

Evidence:

- `build/validation/character-course-runtime-slices-o2-build.log`
- `examples/character_course/build/character-course-runtime-slices-2026-10-07.provenance.json`
- `build/validation/character-course-runtime-slices-package.log`
- `build/validation/character-course-runtime-slices-relocated.json`
- `build/validation/character-course-runtime-slices-relocated.log`

## Reproduction

Use `/opt/homebrew/bin/python3.14` for these commands. Build with
`ELISA_COMPILER_BIN=elisac-stage1`,
`ELISA_RUNTIME_OBJ=$HOME/.elisac/stage1/build/runtime/elisacore_runtime.o`,
`DEVELOPER_DIR=/Library/Developer/CommandLineTools`,
`WICKED_ROOT=../elisa-boxing-wickedengine`,
`WICKED_BUILD=../elisa-boxing-wickedengine/build-elisa-sdl3` and
`ELISA_SDL3_LIB_DIR=/opt/homebrew/lib`.

```sh
python3.14 scripts/elisa_build_run.py build --project examples/character_course \
  --main main.elisa --output build/character-course-runtime-slices-2026-10-07 --optimize
python3.14 scripts/package_macos_app.py --project examples/character_course \
  --executable examples/character_course/build/character-course-runtime-slices-2026-10-07 \
  --output build/CharacterCourse-RuntimeSlices-2026-10-07.app \
  --name CharacterCourseRuntimeSlices20261007 \
  --bundle-id org.elisa.character-course.runtime-slices.20261007 --version 0.1.0 \
  --shader-root ../elisa-boxing-wickedengine/WickedEngine/shaders --compiled-shaders-only
python3.14 scripts/validate_interactive_macos_app.py \
  --app build/CharacterCourse-RuntimeSlices-2026-10-07.app \
  --report build/validation/character-course-runtime-slices-relocated.json \
  --log build/validation/character-course-runtime-slices-relocated.log \
  --timeout 60 --stop-after 3 --resource-group cells --resource-group fonts \
  --resource-group rigs --resource-group sounds --resource-group text \
  --marker 'wi::physics Initialized [Jolt Physics'
```

This is startup/resource/teardown evidence on this host. Manual input in this
exact bundle, a separate clean machine, physical audio listening/device changes,
optional shader paths, release signing/notarization and legal notice review
remain unverified. Audio was explicitly forced unavailable. No new Elisa pure
runtime policy was introduced by packaging; no proof sweep was performed here.
The linked animation, contact and audio notes retain their proof scope and
unproved limits. This closes local targeted slice 6, not Q02 or the full plan.
