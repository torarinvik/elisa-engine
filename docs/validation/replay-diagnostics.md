# Replay and state diagnostics validation

`src/runtime/replay.elisa` records bounded tick-stamped input, random seed, and
world/physics/render digests. Two recordings compare deterministically and return
the first divergent tick, including missing trailing frames. The recorder marks
its determinism scope so same-build guarantees are not presented as cross-build
bit identity.

`test/replay.elisa` covers matching recordings, digest divergence, duplicate
tick rejection, and scope reporting. Input serialization, persistent trace
files, and subsystem-specific digest production remain W09 integration work.
