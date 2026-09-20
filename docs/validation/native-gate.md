# Native-first gate validation

`scripts/native_gate.elisascript` composes the native checks without making
Godot a prerequisite. It accepts `quick`, `headless`, or `native`:

```text
elisascript scripts/native_gate.elisascript quick
elisascript scripts/native_gate.elisascript headless
elisascript scripts/native_gate.elisascript native
```

Every run clears `build/native-gate.json` before doing work and writes a fresh
schema-2 JSON record with dependency, source-length, module-hygiene, headless,
and native stage states. A stage is explicitly `pass`, `fail`, or `skip`, so a
quick run cannot report an omitted native stage as green. The record also
includes the checkout revision, host platform, Python/SDK provenance, timestamp,
and `hardware_verification` (`verified` only for a passing native run). The
quick mode is suitable for a clean checkout policy check; headless adds the
AddressSanitizer/UBSan boundary harness; native adds the SDL3/Wicked graphics,
package, navigation, coordinate, pacing, and deterministic-frame gate.
The launcher resolves every repository check from its own source path, so it
works when invoked from outside the checkout. Native mode runs the Wicked build
and each frame/verification stage in bounded nested processes, so the
ElisaScript process-capture limit cannot cut off a successful long-running
native gate.

On 2026-09-19 the quick and headless modes passed on the updated macOS toolchain.
The native mode remains the workstation command documented in
`docs/native-backend-validation.md`; it requires the configured Wicked build,
SDL3 libraries, and a graphics session. Godot compatibility stays in
`scripts/check.elisascript` and is reported separately.
