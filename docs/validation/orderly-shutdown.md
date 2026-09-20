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

Result: exit status 0; both native frame passes verified topology and exact
determinism, the shutdown-hook job drain and 64-entry lifecycle pressure probe
passed, and frame time stayed within budget. The eight repeated device cycles
grew `malloc_zone_statistics` usage by 1,935,000–1,956,000 bytes, about
270–290 KiB per cycle. The previous 2 MiB limit allowed this linear growth and
is not evidence of a steady state.

With `MallocStackLogging=1`, `leaks` on a paused lifecycle-only process reported
454 live allocations totalling 1,378,384 bytes, including FAudio allocations
from reverb initialization and Metal shader `dispatch_data` created by
`GraphicsDevice_Metal::CreateShader`. `leaks -atExit` on the same lifecycle
probe reported 1,228 allocations totalling 11,764,304 bytes. The report also
contains macOS framework cycles. The Metal command-list roots found before the
destructor patch (`TextureClearBatchItem`, `CommandList_Metal`, and
`GPUBarrier`) no longer appear, but total live heap growth is still
unattributed. `AddressSanitizer` leak detection is unavailable in this macOS
runtime (`detect_leaks is not supported on this platform`).

F05 remains partial. Repeated native host/device restart is not equivalent to
the required in-process scene restart, and persistent process allocations need
to be separated from scene-owned leaks. Keep the measured growth visible until
the repeated-scene test and device-lifetime ownership are accounted for.
