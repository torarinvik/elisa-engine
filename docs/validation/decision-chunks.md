# Chunked decision logs

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler (`ELISA_ALLOW_STALE_STAGE1=1`). This is W09 progress. It follows
[`audio-events-logged.md`](audio-events-logged.md). A decision log holds 256
entries, so a real session, such as footsteps over minutes of play, would
overflow it.

## Design

`src/runtime/decision_chunks.elisa` (`RuntimeDecisionChunks`) splits a long
session into chunks:

- `full` reports that a recorder has reached `CAP`. The caller then seals
  the chunk (for example, saving it with `RuntimeDecisionLogStore` under a
  per-chunk key).
- `continue_after` starts the next recorder with the sealed chunk's seed and
  generator state. Draws are therefore identical to one unbounded log.
- `advance` moves a replay to the next chunk only once the current one has
  been consumed exactly. Before that it returns the replay unchanged. A
  divergence stays recorded when moving on.

## Checks

`test/runtime_decision_chunks.elisa` exits 0. It records 600 draws into
three chunks of 256, 256 and 88 entries.

| Code | What it checks |
| --- | --- |
| 1–2 | Chunks match a single continuous generator draw for draw, with no overflow. |
| 3 | Replay across all chunks consumes every draw and is complete. |
| 4 | Advancing before a chunk is consumed is refused. |
| 5–6 | A tampered second chunk diverges at tick 266, and moving on does not clear it. |

Negative controls:

- Restarting the generator state in each chunk exits 2.
- Allowing an early advance exits 4.

The test is wired into `scripts/check.elisascript`, and the source-length
check passes.

## Course wiring

- `CourseSounds` now fires every event through `AudioEventsLogged` into a
  chunked decision log. It seals a chunk when the log fills, and
  `CourseSounds::decisions` reports the total across chunks.
- The board and log both use seed 0, so the variants heard are unchanged.
- To keep `sounds.elisa` under 600 lines (595), its pure mixing helpers
  moved to `examples/character_course/sound_mix.elisa` (`CourseSoundMix`).
- The live weighted-jump self-test asserts that all 200 jumps logged one
  decision each.
- `scripts/application_native_smoke.py --only
  character-course-smoke,character-course-relaunch-smoke` passes.
- Negative control: expecting 201 decisions fails the smoke with status 169.

## Gaps

- Chunk persistence (saving each sealed chunk under its own key and naming
  the chunk index) is left to the caller. There is no native test of a
  multi-chunk save and load yet.
- The course records its sound decisions but does not save them or replay
  a session from them. Only the chunk count is exercised live, and the live
  test never fills a chunk (200 < 256).
