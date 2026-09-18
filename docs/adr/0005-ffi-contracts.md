# ADR 0005: FFI retention and ownership contracts

**Status:** accepted (2026-09-17)

## Context

Generated ABI declarations cannot infer lifetime, retention, thread
affinity, or failure behavior from a C header. Guessing any of them
produces use-after-free and cross-thread defects at the boundary.

## Decision

- Six documented operations with fixed contracts: read-only immediate
  input (shared borrow, no retention), in-place output (unique borrow to
  written extent), async upload (completion-owned bytes to a token),
  resource creation (owning handle, partial cleanup on failure),
  callback registration (context live until unregister plus drain), and
  destruction (owning handle, valid phase/thread).
- Contracts are validated as data; they restrict safe callers but do not
  make unverified native code safe. Exceptions never cross the C ABI;
  recoverable failures become explicit error values.
- Adversarial behavior (retention, callbacks, errors, destructor order)
  is exercised against a deterministic fake bridge before any real
  native library is introduced.

## Evidence

`src/backend/contracts.elisa`, `src/backend/fake_bridge.elisa`,
`test/contracts.elisa`, `test/fake_bridge.elisa`.

## Not covered

Sanitizer/fuzz runs against real native libraries; vendor internals
(Godot reference counting, renderer worker pools) stay behind wrappers.


## Adversarial bridge coverage extended (2026-09-18)

The fake bridge now bounds upload size (`MAX_UPLOAD_BYTES` and an
`OversizedUpload` error, checked before slot allocation so an oversized request
is not mistaken for capacity) and `test/fake_bridge.elisa` covers the remaining
adversarial cases the plan lists: resource, upload, and callback capacity
exhaustion; a stale callback handle after its slot is reused; a stale upload
ticket after its slot is reused; and ending a callback that was never started.
