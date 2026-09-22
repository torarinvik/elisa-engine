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
publishing the result. `test/replay.elisa` covers matching recordings, digest
divergence, duplicate-tick rejection, frame round trips, short-frame rejection,
and invalid-frame rejection. The native probe also exercises durable write,
read, comparison, and checksum-corruption rejection. Subsystem-specific digest
production remains W09 integration work.

The portable frame test is:

```sh
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
"$HOME/.elisac/elisac-stage1" -emit exe -o build/replay-test \
test/replay.elisa && build/replay-test
```
