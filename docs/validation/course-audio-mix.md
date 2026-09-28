# Character course audio mix and weighted variants

Validated on 2026-09-28 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler (`ELISA_ALLOW_STALE_STAGE1=1`), SDL3/Metal, Wicked and Jolt.

This is S05 progress in the second game. It extends
[`course-audio.md`](course-audio.md).

## Design

- **Mixer snapshots.** `src/audio/mixer.elisa` (`AudioMixer`) is pure policy.
  - A `Snapshot` holds Music, Effects and Ui gains. `valid` accepts gains in
    [0, 1] and rejects NaN.
  - A `Mix` ramps linearly from one snapshot to a target over up to
    `MAX_RAMP_TICKS` (3600) ticks. A zero-tick ramp is a cut.
  - `transition` starts from the gains `current` reports at that tick, so
    interrupting a fade never jumps.
- **Weighted variants.** `AudioEvents` definitions now allow at most
  `MAX_VARIANTS` (4) variants.
  - `set_weights` gives an event a weight per variant. Weights past its
    variant count must be zero and at least one must be positive.
  - A weighted event picks its variant from a Park–Miller roll (modulus
    2^31−1, multiplier 48271) over the board seed, the event id and the tick.
    The same seed and tick always pick the same variant, including after
    `reset`.
  - Events without weights still rotate in order.
- **Course.** `CourseSounds` keeps a `Mix` and a mood.
  - Pause ducks the music to 0.45 and the controls menu to 0.2. Effects go to
    0.3 in both, so ambience fades back in on resume. Ui stays at 1.0.
  - The play loop calls `mix` every frame with the mood and a millisecond
    clock that keeps running during pause. A new mood starts a 250 ms ramp;
    the heard gains go to `AudioRuntime::set_bus_gain`.
  - `stop` restores full gains.
  - Jump variant A is weighted 3:1 over B, with a fixed seed.

## Checks

`test/audio_mixer.elisa` and the extended `test/audio_events.elisa` run in
`scripts/check.elisascript`. Codes 166–171 run in the course self-test after
the pointer checks.

| Test | Code | Check |
|---|---|---|
| mixer | 1–2 | a valid snapshot mixes; gains above 1, below 0 or NaN are rejected |
| mixer | 3–4 | a new mix is settled; an invalid target or a ramp over the limit is rejected |
| mixer | 5–7 | a 10-tick fade reads the start before and at its first tick, the half-way gains at tick 5 and the target from its last tick on |
| mixer | 8–9 | interrupting at the half-way tick fades back from the half-way gains |
| mixer | 10 | a zero-tick transition is immediate |
| events | 8–9 | five variants are rejected and four are accepted |
| events | 60–61 | weights for an unknown event, weights past the variant count and all-zero weights are rejected |
| events | 62–63 | with weights 1:0:3 over 400 ticks, variant 1 never plays and variant 0 plays 80–120 times (100 measured) |
| events | 64 | the same tick after `reset`, and on a fresh board with the same seed, picks the same variant |
| events | 65 | seeds 7 and 8 differ on some of 64 ticks; the same seed never differs |
| course | 166 | moods map from menu and pause, and pause and menu duck the music in order with Ui at full |
| course | 167 | without audio, pause at 1000 ms reads the play gains then, lies between them at 1125 and reaches the paused gains at 1250. Repeating the mood keeps the ramp, and opening the menu mid-ramp starts from the heard gains |
| course | 168 | with live audio, every mood reaches the buses |
| course | 169 | 200 jumps play variant A 130–170 times |
| course | 170 | a scene reset replays the same variant for the same tick |
| course | 171 | stop releases everything |

Commands:

```
ELISA_ALLOW_STALE_STAGE1=1 ../Elisa-compiler/scripts/elisac_stage1.sh -emit exe -o build/audio_mixer-test test/audio_mixer.elisa && build/audio_mixer-test
PYTHONPATH=scripts python3.14 scripts/application_native_smoke.py --only character-course-smoke,character-course-relaunch-smoke
elisascript scripts/check.elisascript
elisascript scripts/native_gate.elisascript native
```

The mixer and events tests, the course self-test, both smokes (`smoke=0`), the
full check (`check=0`) and the native gate (`gate=0`) pass.

## Negative controls

All controls were reverted.

- **The mixer transition starts from the old target instead of the heard
  gains.** The mixer test fails at 8.
- **The ramp limit is off by one.** The mixer test fails at 4.
- **The gain check is written so that NaN passes.** The mixer test fails at 2.
- **The roll ignores the tick.** The events test fails at 63.
- **The roll ignores the seed.** The events test fails at 65.
- **There is no variant bound.** The events test fails at 8.
- **The roll ignores the weights (uniform over three variants).** The events
  test fails at 63. With the first band of 60–140 this control passed (139
  measured), so the band was narrowed to 80–120.
- **The course restarts the ramp every frame.** The self-test fails at 167.
- **Jump weights are 1:1.** The self-test fails at 169.

## Gaps

- The bus gains the course sets are not read back. `AudioRuntime` has no gain
  getter, so 168 checks that every bus accepted the gains, not what is heard.
- Snapshots and weights are code constants, not authored event assets.
  Footfall, animation and physics triggers are still open.
- No listening test is recorded for the duck levels or the ramp length.
