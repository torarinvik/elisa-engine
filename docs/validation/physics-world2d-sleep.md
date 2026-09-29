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
was no ground friction then, the struck ball kept rolling. Negative control:
not storing the woken state made the test exit 4, with the struck ball
frozen.

## Ground friction (2026-09-29)

`Physics2dCircles::ground_friction` slows a body resting on the ground by
mu * g * dt toward zero without overshooting, and does nothing in the air.
The sleeping step takes a friction per-mille and applies it after the
ground contact. The test now expects both balls to stop and fall asleep
again, and checks one friction step's exact slowdown, stopping a slow body
at zero, and no effect in the air. Negative control: removing the friction
call from the step makes the test exit 4.

## Gaps

Friction is ground-only (no body-to-body friction or rolling rotation), and
there are no islands beyond direct contact, so P10 stays open.
