# Elisa project settings

`elisa.project.json` gives a small Elisa project a reusable entry point,
executable path, and default native application settings. The initial format is:

```json
{
  "name": "minimal-application",
  "main": "main.elisa",
  "output": "build/minimal-application",
  "application": {
    "title": "Elisa Application",
    "width": 960,
    "height": 540,
    "hidden": false
  }
}
```

`scripts/elisa_build_run.py build|run --project <directory>` reads this file.
`main` and `output` are relative to the project unless absolute. Explicit
`--main` and `--output` command-line values take precedence. When `output` is
omitted, the runner writes `build/<name>` (using the project directory name if
`name` is omitted). The application section defaults to title `Elisa Engine`,
1280 by 720 pixels, and a visible window. Titles must be 1–255 UTF-8 bytes;
dimensions must be integers from 1 to 16384; `hidden` must be a boolean.
The native runtime compiles at `-O0` for debuggability; `--optimize` (or
`ELISA_NATIVE_OPTIMIZE=1`) compiles it at `-O2` for measurement and release
builds. A Release WickedEngine build, configured with the x86 SIMD options
off on Apple silicon, pairs with it through `--wicked-build`.

For `run`, the runner passes validated settings as private process environment
values. `Application::default_config()` reads those values through versioned,
scalar C ABI accessors; malformed native environment values fall back to the
same engine defaults. Games can still construct and pass their own `Config`.
The ABI exposes no JSON or platform window types to Elisa.

The runner includes `src/runtime/public.elisa` by default so its modules are
available to project entry files. A project that includes its runtime modules
explicitly can pass `--no-public-runtime`; the generated entry then compiles
only the modules reachable from that source. This is useful for focused native
clients and for projects that want an explicit compile surface.

Validation covers manifest path resolution, command-line precedence, spaces in
paths, UTF-8 titles, invalid values, process environment delivery, and native
window startup with dimensions read from a temporary project manifest:

- Implementation commit: `34ca176` (`Add Elisa project manifest settings`).
- `python3 scripts/test_elisa_build_run.py` — passed on 2026-09-20 (6 tests).
- `DEVELOPER_DIR=/Library/Developer/CommandLineTools elisascript scripts/wicked_probe.elisascript build` — passed on 2026-09-20, including runner tests, lifecycle smoke, and live render-scene smoke.
- `DEVELOPER_DIR=/Library/Developer/CommandLineTools python3 scripts/application_native_smoke.py` — passed on 2026-09-20 using SDL3, Wicked, and Metal.
- `DEVELOPER_DIR=/Library/Developer/CommandLineTools python3 scripts/elisa_build_run.py build --project examples/minimal_application` — passed on 2026-09-20.

This is only the runner's minimal project-settings slice. Asset catalogues,
scene persistence, project creation, editor workflows, package relocation, and
manifest version migration remain open work under E04 and Q02.
