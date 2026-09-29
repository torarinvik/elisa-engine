# 2D body sleeping

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the Stage1 compiler.
This is P10 progress, alongside [`physics-world2d.md`](physics-world2d.md).

## Design

`src/physics/sleep2d.elisa` (`Physics2dSleep`) puts a 2D body to sleep after
its speed stays under a threshold for a set number of consecutive steps.
A sleeping body's velocity is zeroed and `integrate` skips it. Any fast
step resets the quiet counter, `wake` clears sleep explicitly, and static
bodies never sleep. The counters are integers, so a scene falls asleep on
the same step on every run.

## Checks

`test/physics_sleep2d.elisa` exits 0: a dropped ball bounces, settles and
sleeps after at least 30 quiet steps; gravity no longer moves it; a second
identical run sleeps on the same step; waking works once; a fast step after
five quiet ones resets the counter; and a static body never sleeps. Negative
control: not resetting the counter on a fast step makes the test exit 5.

## Gaps

No islands (touching bodies do not wake each other) and not yet used by
`world2d.elisa`, so P10 stays open.
