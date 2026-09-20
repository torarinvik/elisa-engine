# Elisa application lifecycle ABI

`src/runtime/application.elisa` is the public Elisa surface for a process-level
native application. It exposes generic configuration and pump statuses. Its
versioned C symbols live in `native/application_abi.h` and
`native/application_abi.cpp`, which own the SDL3/Wicked `NativeApplication`
instance and do not expose vendor types to Elisa callers.

The application author provides the ordinary Elisa `main()`. The engine-owned
`scripts/elisa_build_run.py build|run` command accepts a project directory.
Main and output paths, and default title/window options, come from
`elisa.project.json`; command-line main/output arguments override the manifest.
The runner includes `src/runtime/public.elisa`, which
provides `Application`, `ActionInput`, `RenderScene`, and `Geometry` to the
project without requiring engine-relative include paths,
compiles the Elisa entry point to an archive, rejects game-owned C exports from
the compiler's ABI manifest, and links that archive with the shared native
facade. All child tools receive argument arrays; project, source, dependency,
and output paths may contain spaces. `run` starts the executable with the
project directory as its working directory.

The native gate's `test/application_native_main.elisa` initializes a hidden
window, pumps exactly one frame, verifies the frame count, requests exit,
observes the exit status, and shuts down. `scripts/application_native_smoke.py`
invokes the same generic runner against the pinned native libraries. The smoke
is also part of `elisascript scripts/wicked_probe.elisascript build`.
`scripts/test_elisa_build_run.py` uses fake tools under temporary paths with
spaces to check CLI help, argv preservation, project working directory, and
that a failed compile never runs an old output executable.

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

The lifecycle layer also queues ordered SDL keyboard and mouse-button edges for
Elisa through `Application::next_input_event`. Focus loss and minimization add
a clear-state event; queue overflow produces a reset event so missed releases
cannot leave actions stuck. The scalar token currently carries digital code and
edge state. `ActionInput` remains an Elisa-owned action state API; game source
maps portable device IDs and codes to its action bindings. Analog values,
chords, and gamepad delivery still need a compiler-compatible public path.
`ActionInput::clear_device_state` can clear keyboard, mouse, and gamepad state
without marking devices disconnected. `RenderScene` provides
the first Wicked-backed generic primitive, instance-transform, visibility,
and orthographic-camera service. Mesh/texture import, lighting, input-device
polling, and higher-level rendering features remain future engine work.

Validation: `scripts/application_native_smoke.py` verifies Elisa-authored code
can initialize, pump one frame, read timing/window metrics, observe and consume
a close-request edge, and shut down without game-owned C exports. The ordinary
`test/action_input.elisa` suite covers focus-style held-state clearing while
keeping a device connected.
