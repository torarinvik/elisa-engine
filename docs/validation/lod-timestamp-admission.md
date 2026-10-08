# Reject unwritten and nonpositive profiling timestamps

The LOD completed-frame and quality-profile GPU readers previously rejected
only reversed pairs. A zero begin could therefore turn GPU clock origin into
an elapsed duration, and equal samples could report zero as a measured interval.
Both readers now use `elisa::capture::valid_gpu_timestamp_pair`, requiring a
nonzero begin, a strictly later end and a known nonzero frequency. Invalid
samples clear the retained query state and return the existing backend failure.

## Qualification

The shared native admission regression passes all 11 valid/invalid cases and
three removed-guard controls in 1.19 seconds / 40,720 KiB under 1 GiB. Command:
`python3.14 -c 'import sys; sys.path.insert(0, "scripts"); import native_unit_tests; raise SystemExit(native_unit_tests.run_capture_timestamp_pair_test(["/usr/bin/clang++"]))'`.
Log: `build/validation/lod-timestamp-admission-controls.log`.

The full native renderer main, using frozen compiler `52d60fcf`, its matching
runtime and Wicked `2601ae28`, passes in 60.13 seconds / 1,380,336 KiB under the
original 3 GiB cap. Both LOD levels retain 21 positive GPU intervals. Command:
`python3.14 scripts/render_scene_native_smoke.py --only native` through the
existing watchdog and pinned compiler/runtime environment. Evidence:
`effect-memory-vm-domains-native.log`, watchdog JSON, and
`lod-timestamp-admission-products.json` (actual source/binary hashes).
The native unit controls exercise the shared admission helper; invalid pairs
were not injected into the hardware profiling readers in this run.

The preceding native renderer run fails at the unchanged effect-memory group
238 case 16: 8,208 KiB peak growth against 8,192 KiB. Its heap/GPU allocations
stay nearly constant while process footprint rises and falls; pipeline jobs are
idle at sample points. Preserve `lod-timestamp-admission-native.log` and
`lod-timestamp-memory-failure-terminal/`. No tolerance change or performance
gain is claimed. VM/graphics ledger observations now extend that diagnostic.

`proof/application_capture_timing.elisa` retains the pure conversion scope
(11 original obligations), not a formal proof of C++ timestamp admission or
GPU command ordering. Native admission is covered by executable controls;
formal source linkage for that native predicate remains a qualification task.
Full native gate and full compiler/prover compatibility remain open.
