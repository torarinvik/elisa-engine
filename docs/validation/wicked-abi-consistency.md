# Wicked static archive ABI consistency

The 2026-09-24 macOS crash report for the labyrinth ended in
`wi::ecs::ComponentManager<wi::scene::MaterialComponent>::Clear()` during
`wi::scene::Scene::Clear()` from `elisa_render_scene_v1_shutdown`. A native
reproducer that created and removed a decal also failed with the current
optimized `libWickedEngine.a`, while the same reproducer passed with the
standard SDL3 debug archive.

Inspection of demangled symbols showed why: the optimized Wicked archive had
libc++ ABI tags `220106` and `230101` in one static library. The linked Jolt and
Utility archives used `220106`. This release archive had objects built with
Apple Clang and selected objects rebuilt with Homebrew LLVM after Apple Clang
crashed compiling them at optimization. The archive therefore combined C++
objects with incompatible standard-library ABI layouts. The evidence points
to this mixed-toolchain archive as the cause of the teardown fault; rebuilding
from one consistent toolchain is required before using an optimized archive.

`scripts/check_wicked_archive_abi.py` extracts libc++ ABI tags from demangled
archive symbols and checks the WickedEngine, Jolt, Utility, FAudio, and LUA
archives. It also compiles a small probe with the selected native linker
compiler and compares its ABI tag with the archives. The ordinary Elisa
project runner performs this check before cooking declared assets or compiling
the game, and the RenderScene smoke repeats it before compiling or launching
the native test. On macOS the runner defaults to `/usr/bin/clang++`, matching
the standard SDL3 Wicked archives even when Homebrew LLVM precedes it on `PATH`;
an explicit `CXX` or `--cxx` override is still checked. Missing or mixed tag
sets are errors because compatibility cannot be established.
Run the complete gate using the consistent SDL3 build:

```sh
ELISA_RENDER_SCENE_RENDER_ONLY=1 \
WICKED_BUILD="../WickedEngine/build-elisa-sdl3" \
/opt/homebrew/bin/python3.14 scripts/render_scene_native_smoke.py
```

Never append objects built by a second compiler to an existing archive. If the
chosen compiler cannot build one source file at the desired optimization level,
use a consistent lower-optimization build or fix the compiler/build setup, then
rebuild every C++ archive and Elisa's native bridge with the same compiler and
standard-library ABI. Re-run the ABI check and shutdown reproducer before
shipping that optimized build.

## Consistent optimized build on macOS 27 (2026-09-26)

A fresh `build-elisa-sdl3-homebrew` build with Homebrew Clang 23.1.1,
`RelWithDebInfo` (`-O2`), SDL3 enabled, RTTI and IPO disabled now builds the
`WickedEngine_ext_shaders` target. C, C++, and Objective-C++ all use that
Homebrew toolchain. The ABI gate reports `230101` for the compiler and all
five archives (WickedEngine, Jolt, Utility, FAudio, LUA). Use
`DEVELOPER_DIR=/Library/Developer/CommandLineTools` for this SDK/linker pair.

The first consistent optimized build trapped during `wi::gpusortlib::Initialize`
in `GraphicsDevice_Metal::CreateBuffer2`. Its disassembly contains `brk #1`
immediately after `NS::SharedPtr` releases its initially null object. The
Metal C++ wrappers forward null receivers to Objective-C nil messaging;
CMake now sets `-fno-delete-null-pointer-checks` on `wiGraphicsDevice_Metal.cpp`
to preserve that behavior under optimization. After rebuilding and relinking
with the same toolchain, the complete native texture probe initializes Wicked
and passes KTX2 HDR upload validation. A tiny standalone wrapper test did not
reproduce the failure; the full optimized native probe is the regression check.

Two SDK compatibility fixes are also required: enable Darwin declarations
before `wiHelper.cpp` includes the file-dialog header that selects a restrictive
POSIX namespace, and include `<filesystem>` explicitly in `wiAppleHelper.mm`.
Apple Clang 21 still crashes compiling `wiPrimitive.cpp` and `wiTerrain.cpp`
in the separate optimized Apple build. That incomplete diagnostic build must
not be used as an engine dependency.

The ordinary project runner also disables RTTI for the engine-owned native
bridges, matching the native smoke driver. None of those bridges uses
`dynamic_cast` or `typeid`; requiring `RenderPath3D` typeinfo prevents linking
against `WICKED_ENABLE_RTTI=OFF` builds. The runner test checks this flag.

On 2026-09-26 the ordinary maze project built successfully through
`elisa_build_run.py` with the repository's `bin/elisac-stage1`, automatically
resolved runtime, Homebrew C++, and `build-elisa-sdl3-homebrew`. The installed
September 22 compiler snapshot returned status 2 without a diagnostic on the
same generated entry; the repository compiler compiled it successfully.
This validates the optimized Wicked library with the runner's default `-O0`
bridge, not an optimized bridge or the older compiler snapshot.

The finite `native_smoke_main.elisa` entry was built through the same runner
and passed all nine `packaged_maze_smoke.py` checks: execution with checkout
access denied, denied checkout assets, missing/escaping/corrupted bundles,
missing texture/dependency bundles, and successful execution after restoration.
Use the finite smoke entry for this gate; the ordinary maze waits for input.

## Relocated app with external prepared shaders

On 2026-09-26 `package_macos_app.py` packaged `build/maze-native-smoke`
with `--project examples/maze --name MazeValidation` and
`--shader-root ../WickedEngine/build-elisa-sdl3/WickedEngine/shaders`.
The packager stages this explicit library and derives the manifest from the
staged bytes. `build/cooked` is optional for projects whose cook declarations
put bundles in their runtime asset directory. Missing explicitly declared
resources still fail packaging. Output/input overlap is rejected before any
existing bundle is removed.

The generated `.app` was copied to a temporary path containing spaces and its
launcher completed with exit 0 under `sandbox-exec`, denying reads and writes
to the entire Elisa Projects directory and `/opt/homebrew`. Thirteen packaging
tests pass, including external shader staging, asset-local cook output, and
input preservation. This is a same-machine relocation check using the finite
maze smoke, not a clean-machine, distribution-signing, or optimized-bridge
Release qualification.

## Optimized bridge and offline relocation

On 2026-09-26 the finite maze was rebuilt with the repository Stage1 compiler
and `elisa_build_run.py --optimize`, producing an `-O2 -fno-rtti` native bridge
linked to the consistently optimized Homebrew Wicked archives. Its packaged
`MazeReleaseValidation.app` passed two independent relocated launches with
source projects, Homebrew, and outbound networking denied. The finite entry
runs the maze twice per process, exercising initialization and teardown.

The repeatable check is:

```sh
/opt/homebrew/bin/python3 scripts/validate_standalone_macos_app.py \
  --app build/MazeReleaseValidation.app
```

The checker copies the app to a temporary path with spaces, verifies denied
reads against actual source/Homebrew control files, clears inherited engine and
dynamic-loader overrides, and bounds each launch to 120 seconds. It requires
a finite entry point. This qualifies the tested optimized workload on the
current machine; clean-machine behavior, release signing/notarization, and
broader game workloads remain separate requirements.
