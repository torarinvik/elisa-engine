# Current implementation evidence and open gates

This is the live release record. The prioritized sequence and acceptance criteria are in [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md); earlier detailed snapshots remain available in the [runtime/compiler archive](docs/validation/implementation-evidence-archive-01.md), [grant/proof/Studio archive](docs/validation/implementation-evidence-archive-02.md), and [compiler candidate archive](docs/validation/implementation-evidence-archive-03.md).

## Current evidence and open gates

## Current default-grant compiler candidate — 2026-10-09

Compiler source 06e945842698b713ec20a4a9dba9a90ea5823dc9 is clean. Stage1 SHA256 is 6fcfb4510ad8847d3e3250faaa743d30a3dbb8eb935255ce5373d192f715cc89; matching runtime SHA256 is 961054709fab91faf2fa62876f1f935a7b0948d586eb5b4dec36c9a3c67917aa.

The actual CLI qualification passes 42/42 default Global.Read/Write denial and explicit -permissive bypass controls. Strict engine entrypoints pass 89/89. The same product/runtime passes the uncached engine gate: 216 tests, zero cached or remote, in 55 seconds. The nested global-rehome fixture hash fecfbb561c2df4be342249a17529c6dabba66b3552e2413b0cacd5bd7f6ad96e passes O0, O2 and ASan with default grants.

Using pinned Stage0 source ef04267d736eae492c05a023182f6b08f74441a9 and binary SHA256 d2b6da8722ff2b119eeb8bdd9c41e87c4a0d2e08f25dfb1ddac68d6601be38c, candidate and fresh GitHub main both report the same 28/579 AST parity differences. This is inherited oracle drift.

The pinned fast profile uses only this Stage1/runtime tuple and the pinned Stage0 pair. Its selected checks are still running; current per-check results and logs are recorded in the candidate gate directory at /private/tmp/Elisa-compiler-fbx-latest/build/result-slot-predicate-r2/current-fast-gate-6fcfb451/. The gate is not complete until every selected check, self-host, JSON O0/O2, deterministic emission and proof integration has a result. Do not promote the tuple from partial green rows.

## Current consumer and proof gates

The focused global-rehome stress fixture passes on the candidate, but the matching Studio build must still verify import, drawing, playback and read-only observer reveal. Keep the earlier callback hidden-region failure separate. Proof admission and independent replay must retain all 265 obligations, five CLI regressions and 73 reports within their original budgets.

The same-source authority-check optimization measured 63.1% less cold semantic-check wall time with effectively flat peak RSS. Complete source review and parity in the compiler repository; this remains separate from the still-unmeasured incremental-reuse deliverable. Renderer lifecycle diagnosis awaits an explicit slot handoff. Package refresh and hosted clean-checkout acceptance follow the compiler and consumer gates.

## Historical records

Earlier evidence, exact identities, commands and limitations remain in the three linked archives above and the corresponding docs/validation notes. This file records only the live candidate and the next release gates.
