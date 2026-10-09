# Ordinary image-cook project rehearsal — 2026-10-08

A fresh project under `build/validation/ordinary-image-cook-project` uses the
ordinary `elisa.project.json` declaration, `images` importer, an authored 2×2 PNG,
and a console entry point. The runner builds the executable with the actual
frozen compiler/runtime `52d60fcf` at O2; no fake compiler or cooker is used.

Command: `/opt/homebrew/bin/python3.14 build/validation/ordinary-image-cook-rehearsal.py`.
Evidence: `build/validation/ordinary-image-cook-rehearsal.json` and five adjacent
`image-rehearsal-*.log` files. Outcomes:

- Fresh ELPK cook and optimized executable build succeed.
- Unchanged declaration/source/output gives a verified cook-cache hit.
- Authored pixel edits invalidate the cache and change package bytes.
- Corrupted package bytes trigger recooking and restore identical expected bytes.
- Malformed PNG bytes fail the actual cooker with status 1 and an actionable
  diagnostic; the last good package and cache remain byte-identical.
- No staged cook outputs remain; the built console program exits zero.

The initial diagnostic expected configuration-error status 2 for malformed image
bytes; the cooker correctly returns runtime failure status 1. The rehearsal was
corrected and rerun in full. This is an evidence refinement, not a source defect.

This establishes real project declaration/cook/cache/failure behavior for image
bundles and console executable generation. It does not establish native public-API
consumption, geometry cooking, relocated application packaging, replacement
compiler qualification, or physical GPU acceptance. Those gates remain open.

## Compiler b11e9121 follow-up — 2026-10-09

Repeated the same ordinary project flow with the current default-grant Stage1
product and its matching runtime. The optimized console build used compiler
source `b11e9121`, Stage1 SHA256
`5888942e08da166185c76a8e6a13d774c3aa57cf290f093f1c9ce03dd60a887e`, and runtime
SHA256 `013d317413defc5ffd2f79fb8dd791db6d6fd6a3217edc45fa62a81f4fc03df8`.
The retained report is `build/validation/ordinary-image-cook-b11e9121.json`; the
individual build logs are `build/validation/image-b11e-*.log`.

Fresh cook, unchanged-input cache hit, edited-image invalidation, corrupted
package regeneration, and malformed-PNG refusal all passed. The failed cook
preserved both the last good package and cache. The generated console executable
exited 0. `build/validation/ordinary-image-native-reader` accepted the actual
ELPK from this run and refused a missing section and mutated payload without
leaving stale texture output.

This upgrades image-cook and standalone native-reader evidence to the current
compiler tuple. It still does not exercise a native public-API game package,
relocated startup, signing, or GPU upload; Q02/Q07 acceptance remains open.

## Production native reader follow-up

A standalone O2 C++ consumer includes the real `native/bundle_texture.h` and
`native/package_manifest.h` and reads the actual project-generated ELPK. It
accepts the albedo image at 2×2 and its empty dependency manifest. It then refuses
a missing section and a mutated payload while clearing seeded output bytes;
no stale texture remains after either refusal.

Command: `/usr/bin/clang++ -std=c++17 -O2 -I native -I /opt/homebrew/include -L /opt/homebrew/lib build/validation/ordinary-image-native-reader.cpp -lzstd -o build/validation/ordinary-image-native-reader`,
then run it against `ordinary-image-cook-project/build/tile.elpk` and a corruption
output path. Both commands exit zero. The source and executable are retained in
`build/validation/`. This is production package/texture-reader acceptance without
GPU upload or the full Elisa Application/native package path.
