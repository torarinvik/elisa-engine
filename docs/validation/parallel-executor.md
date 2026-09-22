# Parallel executor validation

`src/runtime/schedule.elisa` derives deterministic conflict-free waves and
`src/runtime/executor.elisa` remains the serial reference order. The native
`ParallelExecutor` in `native/parallel_executor.h` executes each approved wave
with bounded worker count, releases tasks through a barrier, joins every task
before starting the next wave, and reports task exceptions as failure. A
cooperative `Cancellation` token stops later waves only after the current wave
has joined, so no worker remains detached or outlives the executor call.

The Wicked gate proves two independent tasks overlap, the following wave starts
only after both finish, worker caps reject an oversized wave, an exception does
not get swallowed, and a cancellation request prevents the next wave.
