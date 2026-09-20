# Orderly startup and shutdown validation

`native/native_application.h` now tracks SDL and Wicked initialization
separately. A failed startup closes callback admission, runs registered unwind
hooks, stops Wicked audio while SDL is live, waits for any available GPU work,
destroys the Wicked application, clears the global graphics device, destroys
the window, and quits SDL. The destructor and explicit shutdown share this
idempotent rollback path. After callbacks and hooks stop producing work, the
host calls the pinned `wi::jobsystem::WaitForAllJobs()` before stopping audio
or releasing the device. A shutdown hook submits a job in the lifecycle probe;
the test verifies that the job and its context are complete before teardown.

Pinned Wicked commit `e45123b` also frees placement-constructed command lists
after GPU completion on Metal, Vulkan, and DX12. Their C++ destructors release
the vectors and retained API objects before the allocator returns raw storage.
Wicked commit `857d170` balances the caller-owned `dispatch_data_t` used to load
Metal shader bytecode. FAudio shutdown now releases its engine and caller-owned
reverb effect after destroying the effect-chain voice; the XAudio path keeps its
existing `ComPtr` ownership.

`native/window_lifecycle_probe.h` injects failures after SDL setup, after window
creation, and after Wicked initialization. For each point the native gate checks
that the window, SDL subsystems, and Wicked graphics device return to baseline,
then retries initialization and shuts the host down successfully. A separate
thread holds an admitted callback while shutdown closes admission; the test
checks that the callback drains before reverse-order shutdown hooks execute.
The host now provides a fixed 64-entry shutdown-service registry. Each entry is
a function pointer and service-owned context, so registration does not allocate
and each context remains alive through shutdown. The repeated-host probe fills
all 64 entries, verifies the next registration is rejected, checks reverse
execution order, and confirms the in-flight callback drains first. Lifecycle
telemetry counts startup attempts, successful starts, rollback, shutdown,
callback drains, hook invocations, and peak hook/callback pressure. Three
partial failures each retry successfully; eight hidden host/device cycles run
the full-capacity registry and record process heap usage after each teardown.

On Apple, `NativeApplication` now creates scoped Metal-C++ autorelease pools at
startup, event, frame, fixed-step, fullscreen, and shutdown boundaries. Wicked
job workers also drain a pool after each batch of jobs. Its Apple resource-path
helper has a local Objective-C pool because Wicked initializes shader paths
before `main`, before a host-level pool can exist. This removed the engine-owned
missing-pool traffic found during the first probe; with
`OBJC_DEBUG_MISSING_POOLS=YES`, the remaining warning stack was inside Apple's
AppIntents/LinkServices work queue.

Wicked commit `3a450df` adds the per-job-batch worker pools and scopes its
pre-main Apple resource-path lookups. It also includes the Apple Objective-C++
helpers in the CMake library and propagates the frameworks required by both
that library and its offline shader compiler.

Validation on the pinned SDL3/Wicked Metal build:

```text
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
ELISA_ALLOW_STALE_STAGE1=1 ~/.local/bin/elisascript scripts/wicked_probe.elisascript
```

Result: `scripts/native_gate.elisascript native` exited 0 when launched from
`/tmp` with the pinned ElisaScript/compiler and Command Line Tools SDK. The
schema-2 report at `build/native-gate.json` records `outcome=pass` and
`hardware_verification=verified`. Both native frame passes verified topology
and exact determinism, the shutdown-hook job drain and 64-entry lifecycle
pressure probe passed, and frame time stayed within budget. After the diagnostic
scene is removed, the same live host creates and renders 64 fresh Wicked
scenes. Each restart waits for GPU work, stops the render path before scene
teardown, clears scene-owned components, and verifies the application holds no
active path to destroyed scene data. The first six cycles absorb one-time scene
and Metal resource warm-up; the final 58 are reported separately. In the two
native passes the measured GPU deltas were 0 bytes, with process-heap deltas of
7,520 and 11,520 bytes. Eight host/device cycles measured heap deltas of 555,840
and 4,512 bytes. The larger first-pass delta came from a single ~540 KB step at
cycle four; it did not recur in the repeat pass, so its ownership still needs
attribution. The restart soak shows small CPU-heap growth rather than a perfectly
flat line, and does not replace longer allocation tracking.

With `MallocStackLogging=1`, `leaks` on a paused lifecycle-only process reported
417 live allocations totalling 26,496 bytes, all in three `NSXPCConnection`
cycles rooted in Apple AppIntents/LinkServices frameworks. The earlier FAudio
and Metal shader `dispatch_data` allocation roots are gone. An LLDB stop on
`objc_autoreleaseNoPool` likewise attributed the remaining debug-only warnings
to an Apple `LinkServices` XPC callback on `com.apple.root.utility-qos`, with no
Wicked or Elisa frames. `leaks -atExit` can still report operating-system
framework cycles. `AddressSanitizer` leak detection is unavailable in this
macOS runtime (`detect_leaks is not supported on this platform`).

F05 remains partial. The 64-cycle in-process scene-restart probe verifies
rendering, component cleanup, path detachment, GPU completion, and stable GPU
usage through 58 measured restarts, but process-heap usage grows by 7–12 KB.
One eight-cycle host/device pass also contains an unexplained one-time ~540 KB
step. The known Apple framework allocation cycles are outside the engine; longer
allocation tracking is still needed to attribute the remaining heap changes
before claiming scene and device lifetimes return to a stable resource baseline.
