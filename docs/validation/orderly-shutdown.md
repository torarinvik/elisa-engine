# Orderly startup and shutdown validation

`native/native_application.h` now tracks SDL and Wicked initialization
separately. A failed startup closes callback admission, runs registered unwind
hooks, stops Wicked audio while SDL is live, waits for any available GPU work,
destroys the Wicked application, clears the global graphics device, destroys
the window, and quits SDL. The destructor and explicit shutdown share this
idempotent rollback path.

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
partial failures each retry successfully; two additional hidden host cycles
run the full-capacity registry.

Validation on the pinned SDL3/Wicked Metal build:

```text
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
ELISA_ALLOW_STALE_STAGE1=1 ~/.local/bin/elisascript scripts/wicked_probe.elisascript
```

Result: exit status 0; both native frame passes verified topology and exact
determinism, the 64-entry lifecycle pressure probe passed, and frame time
stayed within budget. The registry itself has no dynamic allocation and the
probe accounts for host-owned resources and callback/hook counts. Byte-level
accounting for allocations internal to the pinned Wicked libraries remains
unavailable through their public shutdown interface, so F05 remains partial
until that boundary is measured or instrumented.
