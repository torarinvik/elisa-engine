# Wicked shader warm-up measurement

`scripts/shader_warmup_benchmark.py` measures two process launches against one
temporary Metal shader-output directory. The first launch starts with an empty
cache (plus a marker file needed by Wicked's path preflight); it records the
shader binaries Wicked compiles and measures its first rendered frame. A second
launch uses the same directory and must produce no additional `.cso` files.
Both launches render nine frames and report the first frame separately from
the median and p95 of the later eight frames. The executable must already be
built by `scripts/wicked_probe.elisascript texture`.

Run it on a macOS machine with the repository's configured Wicked SDL3/Metal
build:

```sh
python3 scripts/shader_warmup_benchmark.py
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

Wicked's Metal pipeline-state objects still live only in process memory in this
path. Reusing `.cso` files avoids repeating shader compilation, but each process
still creates its pipeline states. A persistent Metal binary archive and a
project-specific permutation inventory remain future R13 work; the runtime
shader manifest currently verifies file identity and backend coverage, not
pipeline-cache keys.
