# Elisa asset-loader contract validation

`src/assets/loader.elisa` owns the portable part of asynchronous resource
loading. It coalesces requests by asset identity and variant, assigns
generation-checked handles, exposes `Queued` → `Decoding` → `Uploading` →
`Resident` stages, rejects invalid stage transitions, and keeps a placeholder
until a resource is resident. Cancellation and release are explicit, and a
bounded byte budget evicts resident requests instead of exceeding the budget.

The worker decode and device-thread upload calls are deliberately represented
as separate stage functions. Native backends can attach their queues without
moving file IO or GPU work onto an Elisa frame thread.

## Evidence

`test/asset_loader.elisa` is part of `scripts/check.elisascript`. The current
run passed duplicate-request coalescing, invalid-stage rejection, decode and
upload transitions, placeholder state, 64-byte residency eviction, generation
identity, and cancellation. The full shared Elisa/Godot/proof check exited 0
on 2026-09-19.

`native/native_resource_loader.h` now attaches the contract to the bounded
virtual file service and Wicked device phase. The native gate coalesces a
compressed override read, decodes it into a bounded 1x1 upload payload, creates
a real shader-resource texture, and reports requested/coalesced/decoded/uploaded
counts. Resident release destroys the texture and allows generation-safe slot
reuse; failed and cancelled requests can be retried without returning a stale
handle. Cancellation after IO completion drains the ready package bytes instead
of leaving a request slot occupied. Manifest dependency failures and stale
dependency generations fail before upload.
`pump_io_async` schedules bounded package reads on a worker future, and
`upload_ready` performs decode/upload on the caller's device phase. The virtual
file service marks work as `Reading` while holding its mutex only briefly;
filesystem resolution, manifest traversal, section reads, decompression, and
checksum verification run without that mutex. A 32 MiB fixture lets the native
gate observe an active read, cancel it, and verify the result stays cancelled
when the worker finishes. Mount epochs and dependency generations are checked
again before any result is published. This cancellation does not interrupt a
filesystem call already in progress: it returns immediately and discards the
eventual bytes. CRC-32 uses a table lookup per byte. Production texture
decoders, GPU residency budgeting, worker-pool integration, and interruptible
OS reads remain open.

On 2026-09-21, the `build` and `frame` phases of
`scripts/wicked_probe.elisascript` passed on SDL3/Metal. The native frame gate
exercised worker-side package reads, missing manifest dependencies, in-flight
cancellation, remount-epoch invalidation, texture upload/release/retry, and the
existing rendered-scene and orderly-shutdown checks.
