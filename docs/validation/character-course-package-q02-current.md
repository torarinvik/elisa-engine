# Current-source optimized relocated package (2026-10-07)


The ordinary `main.elisa` entry was rebuilt at clean engine revision
`4bf612fc37e43b39cbbc71a0867888972310fc2e` with native `-O2`, production
probes disabled, Stage1 compiler SHA-256
`2f28e5c9ab40101fbc99c7a0b67f5d86cbba0b1fadf735384767487da60de2b3`, and
runtime object SHA-256
`02d868eb68739e517830684eb89bb820bec16f8d07f92ff7bbacee35ef188cb6`. The
Elisa archive compile took 143.91 seconds and native link took 37.02 seconds.
Build identity is `7e2702b4a9b956e4`; the pre-package executable SHA-256 is
`c38bfd93246b97464ac23011ba179c2ad7c988277091d946f2d449f50604e606`.

It was packaged as `build/CharacterCourse-Q02-2026-10-07.app` with the
compiled-shader-only mode. Its launcher executable SHA-256 is
`b27e36a5c64cd35d723755bbe2400ac59113f2215761f3bb6fe4c9204b2d578b`, and
its Metal shader directory contains 392 compiled binaries. On macOS
27.0.1/arm64, the relocated validator passed in 1.227 seconds while denying
the entire project checkouts and `/opt/homebrew`, blocking outbound network,
and requiring the cells, fonts, rigs, sounds, and text resource groups plus
Metal, Wicked, and Jolt startup markers. SIGTERM entered the normal quit loop;
the launcher logged status 0 and the full process group was reaped. The report
and log are `build/validation/character-course-q02-optimized-2026-10-07.json`
and `.log`.

The exact optimized bundle was also opened visibly from the workspace. P
showed the paused controls, R restarted into the playing controls view, and
Escape quit. The retained launcher log ended with `process_exit_status=0`;
its SHA-256 is
`bdf69804cb8acb763a3046fd2169a8b4046059764d1348b9b0d5cddf951e6052` at
`build/validation/character-course-q02-visible-2026-10-07.log`. The fresh
product closes the clean-source, optimized-build, relocated offline startup,
restart, and teardown gaps on this host. A separate clean-machine run remains
open; this host has no second clean macOS target. Compiled-only mode also lacks
source-compilation fallback, and broader optional shader paths, full vendor
notice closure, and release signing remain outside this run.

Reproduction commands:

```bash
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
ELISA_COMPILER_BIN=../Elisa-compiler/scripts/elisac_stage1.sh \
ELISA_RUNTIME_OBJ=../Elisa-compiler/build/runtime/elisacore_runtime.o \
WICKED_ROOT=../elisa-boxing-wickedengine \
WICKED_BUILD=../elisa-boxing-wickedengine/build-elisa-sdl3 \
ELISA_SDL3_LIB_DIR=/opt/homebrew/lib \
python3 scripts/elisa_build_run.py build --project examples/character_course \
  --main main.elisa --output build/character-course-q02-optimized-2026-10-07 \
  --optimize
python3 scripts/package_macos_app.py --project examples/character_course \
  --executable examples/character_course/build/character-course-q02-optimized-2026-10-07 \
  --output build/CharacterCourse-Q02-2026-10-07.app \
  --name CharacterCourseQ0220261007 \
  --bundle-id org.elisa.character-course.q02.20261007 --version 0.1.0 \
  --shader-root ../elisa-boxing-wickedengine/WickedEngine/shaders \
  --compiled-shaders-only
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
python3 scripts/validate_interactive_macos_app.py \
  --app build/CharacterCourse-Q02-2026-10-07.app \
  --report build/validation/character-course-q02-optimized-2026-10-07.json \
  --log build/validation/character-course-q02-optimized-2026-10-07.log \
  --timeout 60 --stop-after 3 --resource-group cells --resource-group fonts \
  --resource-group rigs --resource-group sounds --resource-group text \
  --marker 'wi::physics Initialized [Jolt Physics'
```
