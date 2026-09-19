# Native-first gate validation

`scripts/native_gate.elisascript` composes the native checks without making
Godot a prerequisite. It accepts `quick`, `headless`, or `native`:

```text
elisascript scripts/native_gate.elisascript quick
elisascript scripts/native_gate.elisascript headless
elisascript scripts/native_gate.elisascript native
```

Every run clears `build/native-gate.json` before doing work and writes a fresh
JSON status record with dependency, source-length, module-hygiene, headless,
and native exit statuses. A stale success cannot survive a failed rerun. The
quick mode is suitable for a clean checkout policy check; headless adds the
AddressSanitizer/UBSan boundary harness; native adds the SDL3/Wicked graphics,
package, navigation, coordinate, pacing, and deterministic-frame gate.

On 2026-09-19 the quick and headless modes passed on the updated macOS toolchain.
The native mode remains the workstation command documented in
`docs/native-backend-validation.md`; it requires the configured Wicked build,
SDL3 libraries, and a graphics session. Godot compatibility stays in
`scripts/check.elisascript` and is reported separately.
