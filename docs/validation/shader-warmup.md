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
files; per-project permutation selection and pipeline archive identity keys
remain open.
