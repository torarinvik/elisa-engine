# Dependency notice inventory update — 2026-10-04

The Wicked build includes Jolt as `libJolt.a`. Vendored Jolt source headers identify
Jorrit Rouwe and `SPDX-License-Identifier: MIT`, but the game's generated notice
catalog did not include Jolt's license text. The catalog now pins the upstream
`LICENSE` at commit `5830c342b90fa087f118aa0f086d541a4950b1d9` and verifies its
SHA-256 (`800abe35d64ad9defd636ff1ee8c961e06f0ebca3ef8d10083e8aa0e8ef86ac3`).
The [upstream license](https://github.com/jrouwe/JoltPhysics/blob/5830c342b90fa087f118aa0f086d541a4950b1d9/LICENSE)
is collected as `Jolt.txt` in both game notice directories.

`native/notice-sources.json` records Jolt as a statically linked component, and
`scripts/check_bundled_notices.py` now verifies declared static-component notices
alongside the transitive dynamic libraries. Both project manifests ship
`third_party/notices/Jolt.txt`. A repackaged optimized Character Course app contains
54 notice text files plus the source catalog. Its additional Noto font notice is
listed in `examples/character_course/notice-sources.json`, pinned to the Google
Fonts OFL at commit `a559a6efcfed22bf50219f52ecefcf20b9522408`, and combined with
the shared catalog by the collector. The shared catalog now records static notice
mappings for all ten archives in the optimized game's build provenance, including
FAudio, Lua, Recast/Detour and ozz. `OffsetAllocator`, `SPIRV-Reflect` and
`stb_vorbis` are added to the collected notices. The Utility archive also uses
the exact blue-noise sampler source from AMD's FidelityFX SSSR v1.3; its pinned
MIT license is now included. The global catalog remains incomplete because the
wider per-object and platform-specific vendor closure still needs review.

## Wicked archive attribution — 2026-10-04

The archive member/source audit found additional shipped code that was not mapped
to its own notice: `wiAudio.cpp.o` includes Wicked's miniaudio v0.11.25 copy;
`utility_common.cpp.o` compiles Wicked's vendored `zstd.c`; the Metal objects use
Metal-cpp and Metal IRConverter headers; and the renderer and GPU sort objects
embed FidelityFX FSR1, FSR2, and ParallelSort code. Wicked's zstd license has a
different copyright line from the Homebrew zstd notice, so it is shipped as a
separate verified text. The source catalog now pins these exact Wicked files and
license excerpts by SHA-256, and both game manifests declare the additional
notices. At that checkpoint, the optimized course bundle contained 59 notice
texts plus its source catalog (60 declared paths).

The archive evidence is specific to the Wicked SDL3/Metal build used by the
Character Course. Other embedded source members, the rest of the copied shader
tree, other platforms, and distribution-obligation review remain open; the
catalog deliberately stays `complete: false`.

## Follow-up header audit — 2026-10-04

A follow-up pass over the same static archives found four more attribution gaps.
`wiMath.h` falls back to Wicked's vendored DirectXMath on macOS, and DirectXMath
includes its vendored `sal.h`; both MIT notices are now shipped separately to
preserve Microsoft's and the .NET Foundation's copyright lines. In Utility,
`utility_common.cpp.o` compiles `minimp4.h`, whose header carries a CC0 1.0 public
domain dedication, and `spirv_reflect.h` includes Khronos's generated SPIR-V
header, which declares MIT. Both project catalogs now ship these verified
notices. The full texts are pinned to the upstream
[DirectXMath license](https://github.com/microsoft/DirectXMath/blob/e2f2b9bddbbc4fd0f6f63586f86b2279213b991d/LICENSE),
[CoreRT license](https://github.com/dotnet/corert/blob/c6af4cfc8b625851b91823d9be746c4f7abdc667/LICENSE.TXT), and
[SPIRV-Headers license](https://github.com/KhronosGroup/SPIRV-Headers/blob/86f980c731e62ae4eaf383d320449d71687936bf/LICENSE). The minimp4 notice is the exact
hash-checked excerpt from Wicked's vendored header.

At this checkpoint, the refreshed Maze bundle contained 62 notice texts plus
its source catalog; the Character Course bundle contained 63 plus its catalog.
The notice checker verified all nine dynamic libraries and ten static archives
in both bundles, with no unmapped files. Global completeness remained false
while broader vendor, platform, shader and distribution reviews stayed open.

## Packaged H.264 and shader resources — 2026-10-04

The next archive pass found both minimp4 and Wicked's own H.264 parser in
`wiVideo.cpp.o`; both notices are now included in the `libWickedEngine.a` map
(minimp4 also remains mapped to `libUtility.a`). The package also
ships the prepared shader directory, including source shaders and compiled
Metal kernels. `check_bundled_notices.py` now verifies declared resource-tree
mappings as well as dylibs and static archives. Both bundles map the shader tree
to verified notices for Microsoft's MIT code, Compressonator and BC6H, FidelityFX
FSR1, FSR2, denoiser and sort sources, MJP's SHforHLSL, NVIDIA FXAA and Gaussian Splatting.
The Gaussian Splatting source notice is paired with the full [Apache 2.0
license](https://www.apache.org/licenses/LICENSE-2.0.txt); MJP's MIT notice is
pinned to [SHforHLSL at commit
e426058](https://github.com/TheRealMJP/SHforHLSL/blob/e426058959123063e13d61a62df6217259b72cec/LICENSE).

The resource audit confirms the shader directory and all 15 mapped notice files
are present in both packages. The refreshed Maze bundle contains 76 notice
texts plus its source catalog; the Character Course contains 77 plus its
catalog. Provenance for shader files with no inline marker and other platform
backends remains open.

The new license sources are pinned to the upstream
[OffsetAllocator license](https://github.com/sebbbi/OffsetAllocator/blob/3d8a0258b960cc597e3f7a64ecb8e788ca8ec816/LICENSE),
[SPIRV-Reflect license](https://github.com/KhronosGroup/SPIRV-Reflect/blob/795778a4da471b1c7bdd8833d5766f4436b3a4a7/LICENSE), and
[stb_vorbis source](https://github.com/nothings/stb/blob/1ee679ca2ef753a528db5ba6801e1067b40481b8/stb_vorbis.c), and
[FidelityFX SSSR license and source](https://github.com/GPUOpen-Effects/FidelityFX-SSSR/tree/34dcacd1feefcfab2855b82e76c7d711f2020a75).

A scan of the 799 shader files in the packaged tree checked the first 100 lines
for copyright, SPDX and license markers. It found 49 marker-bearing files: 8 at
the tree root, 3 under Compressonator, 2 under FSR1, 31 under FSR2, 1 under
ParallelSort and 4 under the FidelityFX denoiser. Each group is covered by the
15 declared shader notices. No separate `LICENSE`, `COPYING` or `NOTICE` file
is present in the tree. This scan does not establish provenance for files with
no inline marker, so the shader catalog remains open.

## Vulkan backend archive audit — 2026-10-04

The pinned Wicked SDL3/Metal macOS build also compiles a Vulkan backend into
`libWickedEngine.a`. The generated arm64 build rule defines the Apple platform,
and `wiGraphicsDevice_Vulkan.h` enables `WICKEDENGINE_BUILD_VULKAN` there. The
archive's `wiGraphicsDevice_Vulkan.cpp.o` contains `GraphicsDevice_Vulkan`
symbols and compiles Wicked's Volk implementation and Vulkan Memory Allocator.
The catalog now maps their verified license excerpts, plus Khronos's generated
Vulkan-Headers attribution and the Apache-2.0 license text, to that archive.

The same build includes `wiGraphicsDevice_DX12.cpp.o` as a CMake source, but its
implementation is guarded by `_WIN32`; this macOS build does not define it, and
the object has no DX12 backend symbols. D3D12 Memory Allocator and Windows-only
DirectX header notices therefore remain in the platform audit, rather than
being claimed as part of this macOS backend. The Windows archive and package
still need their own audit.

The object-level dependency pass also found three omissions in the macOS
archive. `wiHelper.cpp.o` contains symbols from the portable-file-dialogs
header (WTFPL); `wiUnorderedSet.h` defaults independently to the vendored
flat-hash-map templates (Boost Software License 1.0), even though the engine's
unordered-map alias selects robin_hood; and `wiShaderCompiler.cpp.o` includes
DXC API headers whose license points to the LLVM/NCSA text in Wicked's vendor
inventory. Those license and copyright texts are now mapped to
`libWickedEngine.a`. The flat-hash-map attribution is based on symbols present
in the archive, not only on the build's unordered-map setting.

## Validation

- `python3 scripts/collect_dependency_notices.py --manifest native/notice-sources.json --output /private/tmp/MazeWickedNoticeAudit-2026-10-04g-notices --wicked-root ../elisa-boxing-wickedengine` collected 80 hash-verified notices; the merged Character Course collection adds its Noto license for 81.
- Both project manifests declare all collected notices plus `sources.json`: 81 paths for `examples/maze` and 82 for `examples/character_course`.
- `python3 -m unittest discover -s scripts -p test_collect_dependency_notices.py` passed 5 tests, including merged project catalogs and hash-drift rejection.
- `python3 -m unittest discover -s scripts -p test_bundled_notices.py` passed 2 tests, including missing/tampered notices, resource-tree presence and unsafe resource paths.
- `python3 scripts/test_package_macos_app.py` passed 28 tests.
- `scripts/check_bundled_notices.py` verifies all nine dylibs, ten static archives and the shader resource mapping in both bundle snapshots, with no mapping problems. Reports: `/private/tmp/CharacterCourseWickedNoticeAudit-2026-10-04g-notice-report.json` and `/private/tmp/MazeWickedNoticeAudit-2026-10-04g-notice-report.json`. It exits 1 only because `catalog_complete=false` remains intentional.
- The existing verified Maze and Character Course bundle snapshots were refreshed with the new notice bytes only. All 81/82 declared notice paths are present under `Contents/Resources/Notices`; these notice-only snapshots were not launched again.
- The prior 59-notice optimized app passed `validate_interactive_macos_app.py` on macOS 27.0.1/arm64 in 1.237 seconds, with engine source, Homebrew and outbound network denied. All five resource groups were present (`cells`: 20, `fonts`: 1, `rigs`: 2, `sounds`: 13, `text`: 1). The validator stopped it with SIGTERM after startup, so graceful shutdown was not tested. Report and log: `/private/tmp/CharacterCourseWickedNoticeAudit-2026-10-04-startup.json` and `.log`; the refreshed notice-only packages were audited but not launched again.
- `python3 scripts/check_source_length.py` and `git diff --check` passed.
- Proof: not applicable; this slice updates distribution metadata and a Python file-hash audit, with no Elisa policy function. Remaining unproved: the full set of embedded/vendor notices and distribution obligations.

Next: audit the remaining vendored source sets and map each shipped static component
to its complete notice. Do not mark the catalog complete until that review is finished.
