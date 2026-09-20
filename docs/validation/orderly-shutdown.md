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
pressure probe passed, and frame time stayed within budget. After the diagnostic scene is
removed, the same live host now creates and renders four fresh Wicked scenes.
Each restart waits for GPU work, stops the render path before scene teardown,
clears scene-owned components, and verifies the application holds no active path
to destroyed scene data. The two deterministic native passes reported scene
cycle GPU usage deltas of 1,032,192 bytes and 0 bytes, and process-heap deltas of
19,360 bytes and 10,176 bytes after the first cycle. These inconsistent memory
samples do not establish a per-scene leak trend. The two matching eight-cycle
host/device checks grew `malloc_zone_statistics` usage by 1,887,168 and
1,892,816 bytes, about 270 KiB per cycle. The previous 2 MiB limit allowed this
linear growth and is not evidence of a steady state.

With `MallocStackLogging=1`, `leaks` on a paused lifecycle-only process now
reports 417 live allocations totalling 26,496 bytes, all in three
`NSXPCConnection` cycles rooted in Apple AppIntents/LinkServices frameworks.
The earlier FAudio and Metal shader `dispatch_data` allocation roots are gone.
The separate, non-instrumented process heap high-water still rises about 1.90
MiB over eight host/device cycles (about 270 KiB per cycle); this measurement
does not by itself show that those bytes remain live. `leaks -atExit` can still
report operating-system framework cycles. `AddressSanitizer` leak detection is
unavailable in this macOS runtime (`detect_leaks is not supported on this
platform`).

F05 remains partial. The in-process scene-restart probe verifies rendering,
component cleanup, path detachment, and GPU completion, but its memory samples
vary between identical native passes. The process heap high-water also grows
across repeated host/device cycles. Continue with a longer soak and allocation
attribution before claiming scene and device lifetimes return to a stable
resource baseline.
