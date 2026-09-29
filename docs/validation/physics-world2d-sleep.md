# 2D world step with sleeping

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the Stage1 compiler.
This is P10 progress. It joins [`physics-world2d.md`](physics-world2d.md) and
[`physics-sleep2d.md`](physics-sleep2d.md).

## Design

`src/physics/world2d_sleep.elisa` (`Physics2dSleepWorld::step`) is the 2D
world step with per-body sleep state. Sleeping bodies are not integrated. A
resolved contact wakes both bodies, so a sleeper hit by a moving body
responds instead of staying frozen. The report gives resolved contacts,
bodies woken and bodies asleep.

## Checks

`test/physics_world2d_sleep.elisa` exits 0: a resting ball falls asleep; a
second ball rolling into it wakes it and pushes it along; and, since there
is no ground friction yet, the struck ball keeps rolling and stays awake.
Negative control: not storing the woken state makes the test exit 4, with
the struck ball frozen.

## Gaps

No friction, so rolling bodies never stop; no islands beyond direct
contact, so P10 stays open.
