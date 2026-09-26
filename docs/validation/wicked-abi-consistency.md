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
