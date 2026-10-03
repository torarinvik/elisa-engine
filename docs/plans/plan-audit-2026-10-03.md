# Implementation plan audit — 2026-10-03

This audit checks the [implementation backlog](../../IMPLEMENTATION_PLAN.md) against its own agent execution contract.

## Fixed in this change

- **The 600-line rule wasn't enforced on the plan.** `scripts/check_source_length.py` and `scripts/record_validation.py` checked `Elisa_Engine_Architecture_and_Plan.md`, which doesn't exist, so the 808-line plan passed. Both now check `IMPLEMENTATION_PLAN.md`. The R (rendering) and I (input/UI) tracks moved to [backlog-rendering.md](backlog-rendering.md) and [backlog-input-ui.md](backlog-input-ui.md), which brings the plan to 581 lines.
- **Dead architecture link.** The plan's opening paragraph linked to that same missing file. It now links to [docs/adr/](../adr/).
- **Other files over the limit.** `native/render_scene_abi.h` (604 lines, previously allowed up to 640) now keeps its animation declarations in `native/render_scene_animation_abi.h`, and the override is removed. `scripts/elisa_build_run.py` (672 lines) now keeps native toolchain resolution in `scripts/native_toolchain.py`. Every tracked source is now within 600 lines with no overrides.
- **A12** now links its worker design, [a12-import-workers.md](a12-import-workers.md).

## Open: completed items without a validation note (rule 10)

These are `[x]` items with no linked `docs/validation/` note. Their completion text cites commands or commits, but rule 10 requires a linked note that names the commit, command, result, artifact, limitations and proof file.

| Item | Likely evidence to link or write up |
|---|---|
| F01 | `docs/native-integration-inventory.md` |
| F03 | the dependency manifest and `scripts/fetch_dependencies.py` hash checks |
| W02 | `src/world/hierarchy.elisa` tests and proof |
| A01 | asset identity and schema tests |
| A13 | the Godot companion-package validation report |

None of these items has been unchecked. Re-verifying them is a separate task.

## Open: other references

- F-track evidence links to `build/native-gate.json`, a generated, untracked artifact. The link only resolves after a local native gate run.
- The handoff section cites `test/parity/driver_acceptance_smoke.sh`, which is in the compiler repository, not this one.

## Counts

| File | Done | Open |
|---|---|---|
| IMPLEMENTATION_PLAN.md | 43 | 62 |
| backlog-rendering.md | 13 | 7 |
| backlog-input-ui.md | 5 | 5 |
