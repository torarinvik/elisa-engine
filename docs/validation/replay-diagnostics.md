# Replay and state diagnostics validation

`src/runtime/replay.elisa` records bounded tick-stamped input, random seed, and
world/physics/render digests. Each frame has a fixed 48-byte little-endian
encoding, so input and digest values do not depend on host struct layout. Two
recordings compare deterministically and return the first divergent tick,
including missing trailing frames. The recorder marks its determinism scope so
same-build guarantees are not presented as cross-build bit identity.

`native/replay_trace.h` persists up to 256 frames in a versioned binary
container. It writes through a temporary path, uses an explicit checksum, and
rejects invalid headers, unsupported versions/scopes, truncated or trailing
payloads, checksum changes, invalid frames, and non-monotonic ticks before
publishing the result. `Replay::Digest` provides bounded deterministic scalar
mixing for subsystem adapters; it is explicitly diagnostic rather than
cryptographic. `test/replay.elisa` covers matching recordings, digest
divergence, duplicate-tick rejection, frame round trips, short-frame rejection,
invalid-frame rejection, and digest changes. The native probe also exercises
durable write, read, comparison, and checksum-corruption rejection.

The portable frame test is:

```sh
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
"$HOME/.elisac/elisac-stage1" -emit exe -o build/replay-test \
test/replay.elisa && build/replay-test
```
