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
counts. Cancellation and stale dependency generations fail before upload.
Worker-thread scheduling and a production texture decoder remain open; the
native pump is intentionally bounded and does not block an Elisa frame on IO.
