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
