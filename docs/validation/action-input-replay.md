# Action-input replay

Validated on 2026-09-30 with the pinned Stage1 compiler. This is I01 progress
toward "replayed input behaves consistently".

`src/runtime/action_input_replay.elisa` records tick-stamped entries for
input events, context switches and device connect/disconnect. Games call
`feed_event`, `feed_context` and `feed_connection`. Each call records the
entry and applies the same entry to the live `ActionInput::Input`. Analog
values are quantized to the Application queue's 20 fractional bits before
both uses, so live and replayed values match exactly.

`replay_tick` applies one tick's entries after `ActionInput::begin_frame`.
The log holds 1024 entries. An entry with a backwards tick, or one past
capacity, still reaches live input but is not recorded. It is counted in
`dropped`, and `replay_trusted` then reports false.

## Test

`test/action_input_replay.elisa` is in the gate's unit-test list. It runs a
12-tick live session:

- jump is pressed, held, released and pressed again;
- the gamepad stick is pushed below and then above its 0.2 dead zone;
- the gamepad is unplugged mid-push and reconnected;
- the UI context opens, Esc fires its menu action, and gameplay resumes.

For each tick, the test takes a digest of the held, pressed and released flags
and the quantized value of every action. Replaying the log into a freshly
bound `Input` gives the same digest on all 12 ticks. The test also checks the
backwards-tick refusal, the capacity count (1023 kept and 78 dropped) and
quantize clamping.

Negative control: having the replay skip Disconnect entries makes the test
fail with code 3. A first control mutated `apply_entry`, which live and replay
share, so it could not fail; the recorded control mutates the replay side
only.

## Byte form

`src/runtime/action_input_replay_codec.elisa` writes the log as "EIR1", a
u32 count and 16 bytes per entry, up to 16,392 bytes in total. The test now
replays from the decoded bytes. Decoding fills the log only when every entry
is valid, and each of these damaged inputs is refused with an empty log:

- a truncated length (BadLength);
- an unknown kind byte (BadField);
- a tick that moves backwards (NonMonotonicTick);
- a wrong magic (BadMagic).

Negative control: decoding the pressed flag from the released bit makes the
replay diverge with code 3.

## Gaps

- The log is not merged into `Replay::Recorder`.
- `ActionInputRuntime` does not feed through it yet, so native-host
  recording still needs its own call sites.
