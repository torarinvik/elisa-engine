# Opt-in VM region inventory for effect lifecycle diagnosis

## Purpose and scope

The retained full renderer sequence failed the unchanged 8 MiB footprint budget,
while later domain instrumentation did not show matching heap or tracked GPU
allocation growth. Task-wide counters cannot identify which mappings changed.
`ELISA_EFFECT_MEMORY_VM_REGIONS=1` now adds a bounded VM-map inventory at the
existing footprint samples. Compare baseline and failing-cycle resident/virtual
region totals by SDK user tag to identify the next allocation owner to inspect.
It neither waits for GPU work nor advances frames or drains resource queues.
The normal gate is unchanged when the opt-in flag is absent.

`native/process_vm_regions.h` walks Mach regions and submaps, with 65,536 steps,
64 submap levels and fixed tag buckets (0–255 plus other). It reports completion
explicitly; API failure, invalid progress, overflow or exhausted bounds leave a
partial observation. Aggregation uses fixed storage and checked unsigned sums.
Region resident pages can include shared mappings; these totals are not physical
footprint and must not replace its allowance. `dirtied` means the SDK's pages
that have been dirtied, not a claim of current private dirty memory. VM maps can
change during observation, so a completed traversal is not an atomic snapshot.

## Focused acceptance — 2026-10-08

`test/process_vm_regions.cpp` compiles at O2 with Apple clang, C++17 and
`-Wall -Wextra -Werror`. It observes an actual tagged 4 MiB Mach allocation,
touches every page, verifies region virtual/resident growth, deallocates it and
verifies the tag returns to its original totals. Pure aggregation controls cover
unknown tags, zero page size, multiplication/sum overflow, region-count overflow
and the region budget. Assertions use explicit failing return codes.
The focused runner is registered in `scripts/native_unit_tests.py`; its helper
passes on macOS. Other platforms do not claim Mach observation coverage.

`native/render_scene_abi.cpp` passes a syntax-only compile with test probes enabled
against Wicked `2601ae28beaf8b5e46ebc87507dfd2c1d6f91a82`, using the existing
renderer include and feature flags. Retained log:
`build/validation/process-vm-regions-renderer-syntax.log` (vendor warnings remain).

## Next acceptance

A source-qualified renderer build must exercise the original full sequence with
this opt-in diagnostic and compare tag totals at the actual failing sample.
No full native gate, leak repair, lifecycle acceptance or tolerance change is
claimed. Hold the native diagnostic while the Studio playback/UI checks and
separate UI runtime test are active.

The native run will use `scripts/run_process_budget.py`, which starts a new
process group, samples aggregate RSS from `ps`, and sends TERM then KILL to the
group when a resource limit is reached. Six focused Python controls pass on
macOS, covering successful completion, timeout, RSS termination, descendant
cleanup, and invalid budgets; GitHub's macOS native job runs the same controls.
The exact renderer command, log path and 3 GiB / 180-second limits are recorded
in the ignored preparation JSON. The watchdog report records peak aggregate
RSS, elapsed time, exit status, and termination reason separately from the
renderer lifecycle report.

## Report preparation

`scripts/effect_memory_region_report.py LOG` produces JSON for one complete
baseline/six-cycle run. It selects the original peak footprint sample, reports
growth against the unchanged 8,192 KiB allowance and ranks signed VM tag deltas
by resident growth. Missing/duplicate samples, partial VM walks, invalid tag
buckets and region-count mismatches are refused. A completed tag delta is
allocation-domain evidence, not a footprint attribution or leak claim.

Three focused Python controls pass, including peak selection, positive/negative
VM deltas, missing/partial/duplicate/truncated inventories and the exact allowance
boundary. Retained output: `build/validation/effect-memory-region-report-controls.log`.
These parser controls do not satisfy the native diagnostic run or lifecycle gate.
The upcoming source-qualified full sequence must supply the real log. This Python
reporting operation has no implementation-linked Elisa proof obligation count.
