# Captured call-chain bounds — 2026-10-08

Prover test commit `725ee3b1` corrects the stale rejection expectation in the
call-chain suite. Immutable signed scalar results retain their own contract
bounds when a later call has effects, reads mutable globals or uses a mutable
borrow. Such calls cannot change an already captured scalar value. This is
different from evaluating the same call text again.

Preserved the original programs and bounds in the new positive fixture
`captured_call_chain_effects.elisa`: **33/33 obligations and certificates**,
zero replay gaps. The rejected fixture now asks for result <= 1,999,999 where
both captured results can be 1,000,000. All four original caller locations
remain refused with `ensure-unproven`; **29/29 emitted certificates** replay
with zero gaps. No production predicate or acceptance check was weakened.

The complete `test_deterministic_call_chain.py` passes, including its long
40-call budget control and accepted/rejected widened-result probes. Log:
`build/validation/proof-captured-chain-controls.log`. Paired provenance was
refreshed after the test commit (`proof-captured-chain-clean-build.log`).

Next independently reproduced matrix failure: `collection_pop_value.elisa`
has eight obligations and certificates but only six replay, with gaps in
`take_last` and `take_two` at return lines 6 and 13. Their contexts include
collection-pop traces. Report: `build/validation/collection-pop-725e.json`.
This remains open; no collection production change was made in this slice.
