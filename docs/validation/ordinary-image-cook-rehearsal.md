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
