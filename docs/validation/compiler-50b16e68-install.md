# Compiler 50b16e68 installed; engine qualification pending

The UI chat installed upstream revision
`50b16e6800fe03e3369a78aa85fffb55ee2f38e3` on 2026-10-07. The engine chat
independently read the installed SNAPSHOT and ran its source freshness check:

```
DEVELOPER_DIR=/Library/Developer/CommandLineTools bash /Users/torarinvikbjarko/.elisac/stage1/scripts/assert_stage1_fresh.sh /Users/torarinvikbjarko/.elisac/stage1/bin/elisac-stage1
```

Exit zero. SHA256 of installed compiler:
`a0a9b2951d68c64f3a3c56cf20ff99eee8256af3f0ff467c591cecaac6eb1bae`.
Runtime: `ca40ba1db8a74110936ad5cdaf808707020c5c74ebb6e491bda2198696d13b8a`.
This revision adds audited borrow-exclusivity checks and reference-field
backend lowering. Its engine performance has not been measured.

The mocap branch bridge commit `3c51102b` passes strict extern declaration
compilation for five bounded FBX entry points, plus one linked call at O0/O2
on this compiler/runtime pair. Native boundary, staging, actual conversion,
digest/cache and source/existing-output preservation checks pass. Exact
header and evidence are in that branch's `native/studio_fbx_bounded.h` and
`docs/validation/fbx-bounded-bridge.md`; client staging/cache remains there.

This focused check does not establish shared/native engine gates, a refreshed
prover product pair or the full prover matrix under 50b16e68. Those must be
rerun with matching archived parser/runtime inputs. Earlier 63585c5f evidence
remains historical qualification of that specific pair, not this installation.
Hosted pins remain unchanged pending the complete gates.
