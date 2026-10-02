# Wicked shader warm-up measurement

`scripts/shader_warmup_benchmark.py` measures three process launches against
one temporary Metal shader directory: cold shaders with pipeline capture,
cached shaders alone, and cached shaders with the captured Metal archive.
Each launch renders nine frames and reports the first frame separately from
the median and p95 of the later eight frames. The gate requires a nonempty
archive, successful load, and no additional `.cso` files in either cached run.
Build the executable first with `scripts/wicked_probe.elisascript texture`.

Run it on a macOS machine with the repository's configured Wicked SDL3/Metal
build:

```sh
WICKED_BUILD="../WickedEngine/build-elisa-sdl3-homebrew" \
CXX=/opt/homebrew/opt/llvm/bin/clang++ \
PYTHON_BIN=/opt/homebrew/bin/python3 \
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
elisascript scripts/wicked_probe.elisascript texture
/opt/homebrew/bin/python3 scripts/shader_warmup_benchmark.py
```

The script accepts `--wicked-root` and `--probe` for non-default checkouts and
executables. Each invocation creates its own temporary cache, so running it
again is an independent cold/warm trial.

## macOS 27.0 / Apple M5

Two trials on 2026-09-25 produced the following results:

| Measurement | Trial 1 | Trial 2 |
| --- | ---: | ---: |
| Cold process launch | 13,793 ms | 9,897 ms |
| Shader binaries created on cold launch | 392 | 392 |
| Cold first rendered frame | 5,878 ms | 2,563 ms |
| Cold later-frame median / p95 | 8.8 / 19.1 ms | 8.4 / 11.5 ms |
| Cached process launch | 1,108 ms | 896 ms |
| New shader binaries on cached launch | 0 | 0 |
| Cached first rendered frame | 6.6 ms | 4.9 ms |
| Cached later-frame median / p95 | 7.6 / 8.8 ms | 8.3 / 8.3 ms |

The identical second launch reused all runtime-compiled `.cso` files. Cold
startup and the first frame were much slower, with noticeable trial-to-trial
variation while Wicked compiled its requested Metal permutations. This is a
small native probe, not a representative packaged game workload or a fixed
performance threshold. The 392 runtime-requested binaries are also not the
same set as the 398 permutations emitted by the offline compiler: the latter
prepares additional engine variants.

## Persistent Metal archive (2026-09-26)

The Wicked backend accepts `WICKED_METAL_PIPELINE_ARCHIVE_CAPTURE` for an
opt-in development capture, or `WICKED_METAL_PIPELINE_ARCHIVE` for read-only
loading. Capture records compute, ordinary render, and mesh pipeline
descriptors, then serializes on device shutdown and atomically publishes the
result from a unique temporary file. The destination parent must already
exist. Normal launches leave both variables unset. Configuring both disables
the archive. Missing or invalid archives fall back to ordinary pipeline creation.
The geometry-emulation helper path is not captured. Native fallback checks
for missing/corrupt load files, a missing capture parent, and conflicting
settings each completed nine rendered frames. Dangling symlink and FIFO
capture destinations were rejected without modifying them; both fallbacks
also rendered nine frames.

A consistent Homebrew Clang 23.1.1 optimized build on macOS 27 / Apple M5
produced this trial:

| Measurement | Cold + capture | Shader cache | Shader cache + archive |
| --- | ---: | ---: | ---: |
| Process launch | 20,940 ms | 1,057 ms | 1,052 ms |
| New shader binaries | 392 | 0 | 0 |
| First frame | 527.2 ms | 7.1 ms | 6.8 ms |
| Later median / p95 | 8.0 / 9.1 ms | 8.0 / 8.8 ms | 8.1 / 9.1 ms |

The archive was 60,258,432 bytes. This proves capture and subsequent loading;
it does not establish a meaningful speedup over the driver's existing cache.
A repeat after the final path checks passed with 392/0/0 new shaders, a
60,258,528-byte archive, and launch times of 11,534/792/872 ms.
Cold capture includes the cost of recording pipeline functions and is not
directly comparable to the earlier uncaptured cold trials above.

Project-specific permutation selection, archive identity/invalidation keys,
and packaged archive distribution remain R13 work. Metal can compile cache
misses normally; a load message alone does not prove every pipeline was a hit.
See [the optimized-build validation](wicked-abi-consistency.md) for the
compiler setting required by this Metal wrapper on the current toolchain.

## Offline publication failure handling

Shader preparation now stages the complete replacement library and its
content manifest before changing the project tree. Existing custom shaders
and other backends are preserved. Copy or manifest errors leave the original
tree untouched; a failed publication rename restores it. If restoration also
fails, the error reports the retained backup location. Symbolic links anywhere
in the source or existing shader tree are rejected.

Publication uses two same-filesystem renames, with a brief missing-directory
window. Do not launch readers or run concurrent preparation against that project
during publication. This is rollback protection, not a crash-atomic exchange.
Eight preparation/publication tests pass, including injected copy, manifest,
publication, and rollback failures. A separate real-library check published
398 Metal binaries and recomputed the identical content manifest.

## Generated permutation ownership

Preparation writes a bounded `elisa.metal-generated.json` inventory containing
the paths and SHA-256 digests of compiler-owned outputs. The next preparation
removes outputs absent from the new compiler set and updates the content
manifest. Existing shaders without inventory ownership remain custom files;
legacy libraries gain ownership only for outputs regenerated by preparation.
Modified owned files cause a conflict instead of being overwritten or deleted.
The inventory is build metadata and is excluded from packaged applications.

Eleven shader preparation/publication tests and eleven packaging tests pass.
A real-library rebuild from 398 to 397 binaries removed the omitted output
and changed the shader manifest fingerprint. This addresses stale generated
files; per-project permutation selection and packaged pipeline archives remain
open.

## Project permutation selection

`prepare_wicked_shaders.py --project PROJECT --permutations selection.json`
accepts a nonempty JSON array of paths relative to the Metal output directory,
for example `["objectVS.cso", "nested/variant.cso"]`. Names must match actual
compiler outputs; the example is illustrative, not a complete game shader set.
Malformed, duplicate, escaping, and unknown names fail without publication.
Omitting the option publishes the complete compiler set. Custom files remain
preserved, and formerly generated outputs outside the selection are removed.

The pinned offline compiler still compiles all permutations before selection.
This feature controls publication and package size, not compilation cost. A
project must validate its selection against every supported rendering feature;
the content manifest proves file identity, not workload coverage. Fourteen
shader tests pass, and selected publication of three real Metal binaries
produced the matching manifest. The native-probe workload now has reduced-set runtime coverage below;
broader game-specific workload coverage and compile-time filtering remain open.

## Exporting and verifying an observed selection

```sh
/opt/homebrew/bin/python3 scripts/shader_warmup_benchmark.py \
  --selection-output build/native-probe-permutations.json \
  --offline-shaders ../WickedEngine/build-elisa-sdl3/WickedEngine/shaders/metal
```

After the cold/cache/archive runs, the benchmark selects the observed names
from the supplied offline Metal directory and runs a fourth process using only
that reduced set. It compares the full content manifest before and after
rendering, rejecting added or changed shader binaries. The exported JSON is
compatible with preparation's `--permutations` option and excludes the preflight
marker. Export occurs only after all requested runtime checks succeed.

On 2026-09-26 this gate observed 392 permutations, found all of them in the
398-file offline library, and rendered nine frames with the 392-file subset
unchanged. The fourth launch took 862 ms; an independent run took 908 ms.
This is coverage for the native probe's exercised scene and features, not
evidence that an arbitrary game can omit every unobserved permutation.

## Archive identity keys (2026-09-27)

When an Elisa application supplies a verified shader manifest, the native
application host hashes its exact bytes and sets
`WICKED_METAL_PIPELINE_ARCHIVE_SHADER_KEY` before Wicked initializes. Metal
archive capture writes a companion `.elisa-identity` file containing that key,
the Metal device name, macOS major/minor/patch version, and exact OS build
string. Loading requires an exact match; a missing, stale, or malformed
identity disables only the archive and falls back to normal pipeline creation.
Capturing publishes the identity atomically after the archive. A shader root
without a validated manifest cannot opt into archive capture or loading.

The native warm-up benchmark uses a deterministic digest of Wicked's sorted
shader-source tree as its test key. It captures 5,376 pipeline functions, then
rejects both a changed key and a tampered OS-build identity before loading the
matching archive. The run compiled 392 shader binaries cold, zero on the
shader-cache launch, and zero on the archive launch. Cold/cache/archive
launches took 13,257/737/744 ms; their first frames took 4,076/9/9 ms. The
archive was 60,258,320 bytes. As before, this validates persistence and
invalidation, not a startup speedup over Metal's own cache. The native manifest
test also verifies that the application host derives the key from the
validated manifest bytes.

## Packaged-shader startup, invalidation and cold/warm hitches (2026-10-02)

`scripts/packaged_shader_smoke.py` closes R13. The render smoke runs it after
the packaged maze. It builds `examples/maze/packaged_smoke_main.elisa`, which
runs one host lifetime per process. It then stages that executable, the
maze's two ELPK bundles and a packaged Metal library outside the checkout.
The library holds the 398 compiled `.cso` files of the Wicked revision the
smoke links against, without `.wishadermeta`, plus a schema 2 manifest.

Every launch runs under `sandbox-exec`. The sandbox denies reads and writes
to the whole projects directory, which covers this checkout, the Wicked
sources and every sibling. It also denies writes to the packaged shader
directory. HOME and TMPDIR point at fresh temporary directories.

A control `cat` of a Wicked `.hlsl` source must fail inside the sandbox.
Wicked prints `shader compile FAILED` and the app still exits 0 when it
requests a shader that isn't packaged, so exit 0 alone proves nothing. Each
launch must therefore also print no `shader compile` line and leave the
staged shader tree byte-identical. That is how the smoke shows startup used
packaged inputs only.

**Cache invalidation.** The application host now derives the Metal pipeline
archive key from three inputs, hashed together: the verified manifest bytes,
the backend and the engine build identity (`pipeline_archive_key` in
`native/shader_path_validation.h`). Before this change it used only the
manifest bytes.

- The smoke recomputes the expected key from the manifest and the
  executable's provenance `build_identity`, and requires it in the archive
  identity.
- Adding one permutation to the package and regenerating its manifest makes
  the warm launch reject the archive ("identity does not match") and still
  start.
- Flipping one byte of a packaged shader in place stops startup with
  `Elisa shader manifest rejected`, exit 1, before Wicked starts. Nothing
  falls back to sources.
- The restored package loads its archive again.

The native manifest test checks that a different build or backend changes
the key. `BackendPipelineArchiveKey::archive_fields_reusable` states the
same reuse rule in Elisa. `test/backend_pipeline_cache.elisa` tests it and
`proof/pipeline_archive_key.elisa` proves it.

**Hitch measurement.** With `ELISA_FRAME_HITCH_REPORT=1`, the application
host prints a line at shutdown with these fields:

- `pipelines_us`: synchronous Wicked initialization, where every shader is
  loaded and its pipeline states are created.
- `first_frame_us`: the first pumped frame.
- `later_max_us`: the slowest later pumped frame.

Here "cold" means an empty pipeline archive, captured on that launch. "Warm"
means the second launch, which loads the archive. In the packaged case the
Wicked shader cache is the read-only packaged library, so nothing is ever
compiled at runtime. Three trials on macOS 27.0 / Apple M5 (milliseconds):

| Trial | Cold pipelines | Cold first frame | Cold later max | Cold launch | Warm pipelines | Warm first frame | Warm later max | Warm launch |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 279.1 | 20.2 | 67.6 | 5,658 | 209.6 | 15.1 | 74.3 | 1,890 |
| 2 | 215.4 | 15.6 | 53.6 | 4,909 | 187.9 | 12.6 | 26.6 | 1,748 |
| 3 | 185.7 | 12.4 | 63.8 | 4,720 | 185.8 | 19.4 | 23.4 | 1,531 |

Cold launch time includes serializing the roughly 60 MB archive at
shutdown. That cost, not a startup hitch, makes up most of the gap between
cold and warm launches. Pipeline creation is 9–25% faster warm in trials 1
and 2 and equal in trial 3. First frames are 12–20 ms either way.

macOS keeps its own per-user Metal driver cache, which is shared and cannot
be cleared without touching other applications. Each trial uses a uniquely
named executable, but driver-level reuse cannot be ruled out. For a cold
start that compiles shaders from source, the same day's
`shader_warmup_benchmark.py` run reported:

- Cold, empty Wicked shader cache: 12,450 ms launch with 392 binaries
  compiled; first frame 3,351 ms.
- Shader-cache launch: 825 ms; first frame 87.9 ms.
- Archive launch: 762 ms; first frame 38.1 ms.
- Later frames were about 8.6 ms median in all three.

Known Wicked limitation: in a process that starts a second host after
archive capture published at the first shutdown, Metal command submission
fails (`MTL4CommandQueueErrorDomain error 1`) and the process hangs. That is
why the smoke uses a single-lifetime entry point. Archive capture remains a
development opt-in, and normal launches leave it unset.
