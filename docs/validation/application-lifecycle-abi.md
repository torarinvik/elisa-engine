# Elisa application lifecycle ABI

`src/runtime/application.elisa` is the public Elisa surface for a process-level
native application. It exposes generic configuration and pump statuses. Its
versioned C symbols live in `native/application_abi.h` and
`native/application_abi.cpp`, which own the SDL3/Wicked `NativeApplication`
instance and do not expose vendor types to Elisa callers.

The application author provides the ordinary Elisa `main()`. The engine-owned
`scripts/elisa_build_run.py build|run` command accepts a project directory and
reads the entry point, output path, and optional application defaults from
`elisa.project.json`; `--main` and `--output` can override its paths. It
includes `src/runtime/public.elisa`, which provides `Application`,
`ActionInput`, `RenderScene`, `WorldRendering`, and `Geometry` to the
project without requiring engine-relative include paths,
compiles the Elisa entry point to an archive, rejects game-owned C exports from
the compiler's ABI manifest, and links that archive with the shared native
facade. All child tools receive argument arrays; project, source, dependency,
and output paths may contain spaces. `run` starts the executable with the
project directory as its working directory.

The native gate's `test/application_native_main.elisa` obtains its configuration
through `Application::default_config()`, initializes a hidden window, pumps
exactly one frame, verifies the configured 320 by 200 dimensions and frame
count, requests exit, observes the exit status, and shuts down.
`scripts/application_native_smoke.py` creates a temporary project manifest and
invokes the same generic runner against the pinned native libraries. The smoke
is part of both `elisascript scripts/wicked_probe.elisascript build` and the
native gate's separately reported application stage. It runs the PhysicsRuntime
and Audio fallback checks plus a failure-cleanup client that returns immediately
after an injected assertion; the outer entry shuts down SDL3/Wicked and verifies
that the backend profile is no longer available.
`scripts/test_elisa_build_run.py` uses fake tools under temporary paths with
spaces to check CLI help, manifest defaults and overrides, runtime setting
delivery, input validation, argv preservation, project working directory, and
that a failed compile never runs an old output executable. The native build
stage runs these runner tests before its lifecycle and render-scene smokes.

The native link recipe currently supports macOS with SDL3/Metal. Configure
`WICKED_ROOT` and `WICKED_BUILD` for the engine backend; configure `SDL3_ROOT`
or `WICKED_SDL3_ROOT` and `HOMEBREW_PREFIX` or `WICKED_BREW_PREFIX` for the
package prefixes, or supply the documented individual include/library
overrides. When not set, the runner discovers a sibling WickedEngine checkout
and asks Homebrew for its prefixes. `examples/minimal_application/README.md`
shows the command and all environment settings.

This slice owns lifecycle and one Wicked `Application::Run()` call per pump.
The process has one application instance and calls must stay on its
initialization thread. `Application::FrameInfo` reports elapsed nanoseconds
between pump starts (the first sample starts at initialization), frame count,
logical and pixel dimensions, display scale, and resize serial. Its lifecycle
edge bits report close request, size/scale change, focus gain/loss, minimize,
and restore. Edge bits are coalesced until a successful `frame_info` read;
window flags describe current focus, minimize, fullscreen, suspension, and
close-request state. A resize signal includes display changes that may affect
pixel size or scale, and does not imply only a user drag-resize.
`Application::uptime_nanos()` supplies a monotonic scalar measured from host
initialization and returns zero while the host is stopped. Games can sample it
around scene construction to report startup/load cost without a native timer
shim or wall-clock assumptions.

Review and automation code can read validated `ELISA_*` environment names with
`Application::environment_value` and parse whole decimal values with
`Application::environment_integer`, which returns a caller-provided fallback
for missing, malformed, or unsafe names. After a successful frame,
`Application::save_screenshot` waits for the owner-thread GPU boundary and
encodes the most recently presented Wicked back buffer as an RGBA PNG. Empty,
overlong, wrong-thread, pre-frame, and failed-encode cases return a typed false
or status rather than touching the filesystem arbitrarily.

The lifecycle layer queues ordered keyboard, mouse-button, gamepad-button,
gamepad-axis, and gamepad connect/disconnect events for Elisa through
`Application::next_input_event`. Supported keyboard and gamepad controls use
stable engine codes; game bindings do not need SDL key or gamepad constants.
Stick directions and triggers carry normalized values in `[0, 1]`; the scalar
token preserves those values at 20-bit precision alongside digital codes and
edges. Axis events also release the direction that is no longer active, so
`ActionInput` can apply each binding's own dead zone without leaving a direction
stuck. `InputEvent.connected` reports whether any recognized gamepad remains
after a disconnect. The native host opens up to four recognized SDL gamepads
and closes them before SDL shutdown. Focus loss and minimization add a clear-state event; queue
overflow produces a reset event so missed releases cannot leave actions stuck.
`ActionInput` remains an Elisa-owned action state API and aggregates multiple
bindings while preserving directional intent for the game's opposing-action
calculation. `ActionInputRuntime::begin_frame` clears edge state, drains these
events, and translates them into portable action bindings; chord state is
tracked in Elisa for either key arrival order. Gamepad controls currently share
one logical action device, so per-controller bindings are not yet available.
Full key coverage, mouse movement/scroll, physical controller verification,
and saved rebinding remain.
`ActionInput::clear_device_state` can clear keyboard, mouse, and gamepad state
without marking devices disconnected. `RenderScene` provides
the first Wicked-backed generic primitive, instance-transform, visibility,
and orthographic-camera service. Mesh/texture import, lighting, input-device
polling, and higher-level rendering features remain future engine work.

Validation: `scripts/application_native_smoke.py` verifies Elisa-authored code
can initialize SDL's gamepad subsystem, pump one frame, drain the application
event queue through `ActionInputRuntime`, read timing/window metrics, observe
and consume a close-request edge, and shut down without game-owned C exports.
The application fixture also asserts that uptime is nonzero after initialization.
Its native failure-cleanup client also proves that a returned assertion failure
does not leave the application host active.
The same smoke sets `ELISA_PROJECT_WIDTH=320`, exercises malformed and missing
environment fallbacks, rejects an empty screenshot path, and writes a PNG from
the hidden Metal window after the first frame. The generated PNG header is
checked by `scripts/application_native_smoke.py`.
`test/action_input.elisa` checks public codes and event-to-action behavior, while
`test/application_gamepad_codes.cpp` checks SDL to
portable key/button mappings, signed axis normalization, and digital/analog
token encoding. `test/action_input.elisa` covers focus-style held-state
clearing, per-binding chord state, and an axis falling below its dead zone
while the raw device value remains nonzero. No physical controller was
connected during this validation.

Window flag constants use the `WINDOW_` prefix to distinguish persistent
window state from same-named event bits such as `MINIMIZED` and
`CLOSE_REQUESTED`. This keeps Elisa accessors aligned with the native flag
values.
