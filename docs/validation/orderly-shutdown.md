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

## Follow-up allocation attribution (macOS 27.0, 2026-09-20)

The restart probe now supports `ELISA_SCENE_RESTART_WARMUP_HOLD_SECONDS` as well
as the existing final inspection hold. These pause points let `malloc_history`
sample one process after warm-up and again after the measured cycles.

In a `MallocStackLoggingNoCompact=1` run, cycle 6 reported 387,858,432 GPU bytes
and 57,194,576 heap bytes; cycle 7 reported 422,445,056 GPU bytes and
102,975,184 heap bytes. Both GPU usage and the instrumented heap then stayed
near-flat through cycle 64. Comparing the two live allocation snapshots
attributed most of the added GPU-side work to Wicked's asynchronous object
pipeline cache (`wi::renderer::GetObjectPSO` and its `robin_hood` table) and
Metal command/pipeline setup. The traced snapshot also gained 116 Lua table
allocations (6,496 bytes) through `wi::lua::SetDeltaTime` and
`wakeUpWaitingThreads()`, which creates a per-frame local table.

These stack-logging numbers are deliberately excluded from ordinary heap
results: the instrumentation itself caused a ~45.8 MB heap jump at cycle 7 and
changed GPU allocations. The pipeline cache appears to be expected first-use
state. The Lua allocation was addressed and retested below; neither observation
proves a leak or teardown defect.

## Timer allocation fix and extended soak (2026-09-20)

The allocation stack identified a temporary Lua table created by
`wakeUpWaitingThreads()` every frame, including frames with no expired timer.
Wicked commit `851c8f21cac9e537788590ee6682c88f2511bf56` now scans for an expired
timer before allocating the wake queue. When a timer is due, it still collects the
entire due set before resuming coroutines, preserving safe table iteration and
reentrancy behavior. The native restart diagnostic runs a Lua regression probe:
1,000 idle timer updates must grow the Lua heap by less than 1 KiB, and an expired
timer must resume its coroutine. Both the 64-cycle and 512-cycle diagnostic runs
exited successfully with this probe enabled.

After the patch, the 64-cycle run (six warm-up cycles) measured a 0-byte GPU delta
and an 8,736-byte process-heap increase across 58 restarts. The longer uninstrumented
run used 16 warm-up cycles and 496 measured restarts:

```text
ELISA_SCENE_RESTART_ONLY=1 \
ELISA_SCENE_RESTART_CYCLES=512 \
ELISA_SCENE_RESTART_WARMUP_CYCLES=16 \
build/wicked-native-probe ../WickedEngine/WickedEngine backends/scene_manifest.txt
in-process scene restart: cycles=512 warmup_cycles=16 measured_cycles=496 rendered=1 components_cleared=1 gpu_delta_bytes=0 heap_delta_bytes=5904
```

The same 512-cycle run before the Lua change measured a 66,944-byte heap increase
and a 0-byte GPU delta. After the change, GPU usage remained at 422,445,056 bytes
for the measured samples; the 5,904-byte heap increase is much smaller but is not
yet attributed. The probe verifies scene rendering, component cleanup, path
detachment, GPU completion, and timer behavior; it does not establish a perfectly
flat process heap or explain the separate one-time ~540 KB host/device step. F05
therefore remains partial pending allocation attribution and a repeat long soak.

The complete post-fix SDL3/Wicked gate also passed:

```text
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
ELISA_ALLOW_STALE_STAGE1=1 elisascript scripts/wicked_probe.elisascript
Native frame verified: dimensions, determinism, Elisa topology.
frame time: samples=30 median=1598us p95=2994us worst=3305us budget=16667us
in-process scene restart: cycles=64 warmup_cycles=6 measured_cycles=58 rendered=1 components_cleared=1 gpu_delta_bytes=0 heap_delta_bytes=1824
host lifecycle heap: steady_bytes=147047264 final_bytes=147069328 delta_bytes=22064
churn memory: before_bytes=155733456 steady_bytes=155815952 after_bytes=156093136 steady_delta_bytes=277184
Native live-input frame rendered from the embedded game.
```

The ~540 KB host/device step from the earlier run did not recur. The 22,064-byte
host lifecycle heap increase and 277,184-byte post-churn delta still need
attribution. These results support the targeted Lua fix without proving that all
remaining memory changes are leaks or fully explained.

## Standalone restart warm-up correction (macOS 27.0, 2026-09-20)

A standalone restart-only run starts without the diagnostic scene that normally
warms Wicked's render resources. With six warm-up cycles, the instrumented
profile first observed a 1,032,192-byte GPU-memory increase on cycle 7; usage
then stayed flat. `malloc_history` also showed Metal presentation allocations
under `RenderPassBegin` and `CAMetalLayer::nextDrawable`. The tool attached
stack logging during the warm-up hold, so the capture had no stack traces for
earlier allocations and its heap/footprint values are not used as leak
measurements.

The standalone probe now defaults to 16 warm-up cycles. An uninstrumented
macOS 27.0 rerun passed:

```text
ELISA_SCENE_RESTART_ONLY=1 \
build/wicked-native-probe "$PWD/../WickedEngine/WickedEngine" \
  "$PWD/backends/scene_manifest.txt"
in-process scene restart: cycles=64 warmup_cycles=16 measured_cycles=48 rendered=1 components_cleared=1 gpu_delta_bytes=0 heap_delta_bytes=-3600
```

This makes the standalone GPU baseline stable through the observed first-use
allocation. F05 remains partial because the separate 512-cycle, host/device, and
post-churn heap changes have not all been attributed.

## Latest two-pass native gate (2026-09-20)

After exposing the live capability profile through ordinary Elisa applications,
the full SDL3/Wicked probe passed twice, including the profile query, cooked
geometry reader, rendered-frame determinism, and lifecycle probes:

```text
DEVELOPER_DIR="$(xcode-select -p)" \
ELISA_ALLOW_STALE_STAGE1=1 \
elisascript scripts/wicked_probe.elisascript

in-process scene restart: cycles=64 warmup_cycles=16 measured_cycles=48 gpu_delta_bytes=0 heap_delta_bytes=4128
in-process scene restart: cycles=64 warmup_cycles=16 measured_cycles=48 gpu_delta_bytes=0 heap_delta_bytes=0
host lifecycle heap: delta_bytes=3504 (both passes)
churn memory: steady_delta_bytes=282336 and 272064
Native frame verified: dimensions, determinism, Elisa topology.
Native live-input frame rendered from the embedded game.
```

The scene GPU baseline stayed flat and the previous one-time host heap step did
not recur. The small scene/host heap changes and post-churn deltas remain
unattributed; F05 is still partial, and this two-pass gate is not a substitute
for repeating the 512-cycle soak.

## Gate after compact FBX vertex indexing (2026-09-20)

After FBX skin influences moved into a separate indexing stream, the full SDL3/
Wicked gate passed twice on the updated Mac toolchain:

```text
DEVELOPER_DIR="$(xcode-select -p)" \
ELISA_COMPILER_BIN="../Elisa-compiler/scripts/elisac_stage1.sh" \
ELISA_ALLOW_STALE_STAGE1=1 elisascript scripts/wicked_probe.elisascript

Native frame verified: dimensions, determinism, Elisa topology.
in-process scene restart: cycles=64 warmup_cycles=16 measured_cycles=48 gpu_delta_bytes=0 heap_delta_bytes=560
in-process scene restart: cycles=64 warmup_cycles=16 measured_cycles=48 gpu_delta_bytes=0 heap_delta_bytes=-1680
host lifecycle heap: delta_bytes=3840 (both passes)
churn memory: steady_delta_bytes=282304 (both passes)
frame time: median_us=1842/p95_us=3522 and median_us=1274/p95_us=1585
orderly native shutdown passed
```

Both passes exercised FBX import/cooking, the ordinary application lifecycle,
the render-scene bridge, Wicked library probes, orderly shutdown, and exact
frame comparison. GPU usage stayed flat. The host and post-churn heap changes
remain unattributed; F05 is still partial.

## Native gate after tangent-frame and checked-startup integration (2026-09-20)

The next full SDL3/Wicked gate passed after cooking tangent frames into FBX
packages and adding service requirements to ordinary application startup:

```text
DEVELOPER_DIR="$(xcode-select -p)" ELISA_ALLOW_STALE_STAGE1=1 \
  elisascript scripts/wicked_probe.elisascript

in-process scene restart: cycles=64 warmup_cycles=16 measured_cycles=48 gpu_delta_bytes=0 heap_delta_bytes=4256
in-process scene restart: cycles=64 warmup_cycles=16 measured_cycles=48 gpu_delta_bytes=0 heap_delta_bytes=0
host lifecycle heap: delta_bytes=14080 and 15456
churn memory: steady_delta_bytes=277184 and 279744
deterministic frame comparison: peak=0.0000 mean=0.0000
Native live-input frame rendered from the embedded game.
```

Both passes reported the Apple M5's native profile (`profile=0xb3`,
`formats=0x7`, workers `9/1`). The gate exercised the checked application
startup path, cooked geometry loading, actual Metal scene rendering, and
repeat-frame comparison. GPU usage stayed flat after warm-up. The host and
post-churn heap deltas remain unattributed, so F05 is still partial.
