# Worker-to-main mailbox

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the Stage1 compiler.
This is W08 progress, alongside `src/world/events.elisa`.

## Design

`src/world/mailbox.elisa` (`WorldMailbox`) is a bounded 16-slot ring of
(kind, payload) messages that workers post for the main thread.

- `take` delivers only in the Simulation and Physics phases, where the world
  may be mutated. Input, Animation, Render and Audio deliver nothing and
  leave the queue intact.
- Messages keep post order across wraparound.
- A full ring rejects the post (`Full`) and counts the drop, instead of
  overwriting the oldest message.
- `close` discards what is queued, reports how many, and refuses later
  posts (`Closed`).

## Checks

`test/world_mailbox.elisa` exits 0: phase gating in all six phases, FIFO
delivery, 110 messages in order over ten wraparound rounds, a full ring
rejecting without overwriting, and shutdown. Negative control: ignoring the
ring head when posting makes the test exit 7.

## Gaps

This is the single-threaded policy; there is no atomic cross-thread ring
or wiring into `events.elisa` frames yet, so W08 stays open.
