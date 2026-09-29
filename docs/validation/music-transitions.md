# Music transitions

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler (`ELISA_ALLOW_STALE_STAGE1=1`) and the `elisa-engine-proof` prover.

This is S05 progress. It follows
[`authored-mix-moods.md`](authored-mix-moods.md), which listed music
transitions as a gap.

## Design

- **Policy.** `src/audio/music.elisa` (`AudioMusic`) decides which music cue
  is heard and how the last one fades out while the next fades in. It is
  integer policy over milliseconds, so a replayed or paused game hears the
  same fade.
  - `request` starts a crossfade using the outgoing cue's fade-out and the
    incoming cue's fade-in. Asking for the cue already heard does nothing,
    so a game can request its music every frame without restarting it.
  - Each fade starts from the gain its cue has now (`playing_from`,
    `leaving_from`). A win reversed mid-fade therefore continues from where
    it was instead of popping back to full. A third cue that is still
    fading out is cut off.
  - `reset` jumps to a cue at full gain, as on a restart. `advance`
    saturates at 10 s. `gain_permille` gives each stream's gain: rising for
    the incoming cue, falling for the outgoing one, and 0 for the rest.
- **Authoring.** The `.sfx` asset gains a record,
  `music <cue> <fade_out_ms> <fade_in_ms>`, with up to four cues and fades
  up to 10 s. Bad values give `InvalidMusic`, a repeated cue gives
  `DuplicateMusic`, and the record is validated all or nothing with the
  rest of the asset. A cue the asset leaves out fades in 500 ms.
- **Course.**
  - `CourseSounds` opens a second looped stream, `sounds/music_win.wav`,
    at gain 0. The runtime's two stream slots are now both in use.
  - Every frame, `play_music` requests the victory cue while the phase is
    `Won` and the course cue otherwise, rewinds a newly requested stream to
    its first frame, and sets both streams' gains (0.5 × cue gain, then the
    Music bus mood).
  - Restart calls `restart_music`, which cuts straight back to the course
    loop from its first frame.
  - `sounds/events.sfx` authors `music 0 600 800` and `music 1 400 1500`.
  - `make_sounds.py` synthesises the 8 s victory fanfare, and `--check`
    covers it.

## Proof

`proof/audio_music.elisa` proves that a guarded asset record meets `cue`'s
requires and names a slot below `MAX_CUES`. It also proves that
`gain_permille` stays within 1000 for any transition, including a
hand-built one. It proves with 35/35 replayed.

The proof needed two prover fixes in `elisa-engine-proof`:

- **Construct arguments.** Replay compares struct constructs, so a call
  summary over `f(T{...})` matches its trace.
- **Negated guards.** A failed guard such as `return FULL if elapsed >= span`
  leaves a fact the quotient rule can use, so `elapsed * 1000 / span <= 1000`
  proves.

Nested calls inside another call's arguments still do not discharge its
requires. `gain_permille` binds each clamped value to a local first.

## Checks

- `test/audio_music.elisa` exits 0. It covers:
  - validation;
  - fade-in from silence;
  - an idempotent request;
  - exact crossfade values;
  - saturation;
  - a fade turned around midway, which continues from 750, not 1000;
  - fading to silence;
  - reset;
  - zero-length fades.
- `build/aea-test` (`test/audio_event_assets.elisa`) exits 0. Codes 70–78
  cover:
  - accepted cues;
  - defaults;
  - an out-of-range cue and an over-long fade (`InvalidMusic`);
  - a duplicate cue (`DuplicateMusic`, line 7);
  - a missing field;
  - rejected assets leaving cues intact;
  - plain assets clearing them.
- Negative control: disabling the duplicate-music check makes the asset
  test exit 75.
- The course self-test runs two new checks:
  - Code 196 runs the authored crossfade without audio. At the win the
    gains are 1000/0; 300 ms in they are 500/200; at 1.5 s they are 0/1000;
    after a restart they are 1000/0.
  - Code 197 streams both loops live, crossfades, restarts, and checks that
    stop leaves no stream open.
- Code 197 also asserts zero underrun frames on both streams (`music_status`
  and the new `victory_status`) after the crossfade and restart, so the
  two-loop workload is measured, not assumed.
- Negative control: authoring a 1000 ms victory fade-in fails the course
  self-test with code 196.
- `scripts/application_native_smoke.py --only
  character-course-smoke,character-course-relaunch-smoke` passes, and the
  source-length check passes.

## Gaps

- Cues are tied to game phases in code, and the asset only authors their
  fades. There is no beat- or bar-synchronised switching.
- Hot reload and animation-event triggers remain for S05.
- No audible listening test was run in this session.
