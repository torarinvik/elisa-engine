# Native backend validation

The engine's SDL3 boundary and Godot host contract are checked by the regular
ElisaScript workflow. The full Wicked renderer remains a separate native spike;
its source is kept outside this repository so upstream code and build artifacts
do not become Elisa source.

## Godot

Homebrew Godot 4.7.2 was installed on 2026-09-17. The headless probe runs with:

```sh
godot --headless --path backends/godot --script backends/godot/probe.gd
```

It applies the portable create/update/destroy lifecycle to a real Godot
`MeshInstance3D`, `StandardMaterial3D`, and `Camera3D`, then validates epoch/entity
identity, transform update, and destruction ordering. It does not claim a
GDExtension or pixel-level renderer equivalence.

## Wicked

The upstream checkout was cloned beside this repository at revision
`5e07e3bfd7f89633a468009e0620b14e508dd8b7` (2026-09-17). It is not copied into
the engine repository. The arm64 Debug/O0 CMake build produces the static
Wicked, Jolt, Utility, FAudio, and Lua libraries needed by the probe. The
release build still hits an Apple Clang frontend crash in `wiPrimitive.cpp`,
and the optional upstream `offlineshadercompiler` target needs additional
Apple framework link flags.

The repository contains a small native scene probe in
`native/wicked_probe.cpp`, driven by `scripts/wicked_probe.elisascript`. It
creates an Elisa-named cube and camera, moves the cube, configures a Metal
swapchain through Wicked's `Application` and `RenderPath3D`, renders one hidden
frame, verifies the render target, and removes every entity. The probe uses a
one-shot process because this Wicked revision has no public shutdown API for
its global worker systems; it still checks Elisa-side despawn before exit. This
is a real Metal frame smoke test, not yet a complete GDExtension or game loop.

When the external checkout has been built as `build-elisa-arm-o0`, run:

```sh
WICKED_ROOT="../WickedEngine" \
WICKED_BUILD="../WickedEngine/build-elisa-arm-o0" \
elisascript scripts/wicked_probe.elisascript
```

Set `WICKED_SDL_INCLUDE_DIR` and `WICKED_SDL_LIB_DIR` when SDL2 is installed
outside Homebrew's `/opt/homebrew` prefix.

The current macOS build needs four local compatibility edits in that external
checkout: a `PipelineHash` inequality operator, the SDL2 Apple cursor guard,
and 8-byte alignment attributes for the two FAudio default curves. The probe
also passes `WI_UNORDERED_MAP_TYPE=2` and `WICKED_CMAKE_BUILD` so its ABI agrees
with the CMake libraries. Those edits and flags are deliberately kept outside
this repository; the command above is the reproducible acceptance gate for the
native scene path.
