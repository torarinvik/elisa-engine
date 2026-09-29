# Decision session store

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler. This is W09 progress. [`decision-chunks.md`](decision-chunks.md)
listed multi-chunk persistence as a gap.

## Design

`src/runtime/decision_session_store.elisa` (`RuntimeDecisionSessionStore`)
persists a session's chunked decision log.

- **Saving.** `save_chunk(index, log)` saves a chunk through
  `RuntimeDecisionLogStore` under `decisions-<index>`. Only after that save
  succeeds does it rewrite a one-byte manifest (`decisions-manifest`,
  version 1) with the new chunk count. A manifest therefore never names a
  chunk that was not written.
- **Limit.** A session can hold up to 8 chunks, which is 2048 decisions.
- **Loading.** `manifest()` validates the version, the length and the range.
  `load_chunk(index)` refuses any index at or past the manifest's count, and
  it decodes through the checksummed codec, so a missing, stale or corrupt
  chunk stops the replay.
- **Clearing.** `clear()` removes the chunks the manifest names, then the
  manifest.

## Checks

`test/decision_session_probe.elisa` runs inside
`application-native-smoke`, against the real user-data directory.

- It records 600 draws. That seals two full chunks and leaves a third one
  partly filled, and it saves all three.
- It reads the manifest back (3 chunks) and checks that chunk 3 is refused.
- It replays through `RuntimeDecisionChunks::advance`, loading each next
  chunk from disk. It checks that the 600 values fold to the recorded hash
  and that the replay is complete.
- It then clears the store and checks that both the manifest and chunk 0
  are gone.

`scripts/application_native_smoke.py --only application-native-smoke`
passes.

Negative control: loading the current chunk again instead of the next one
fails the smoke with status 168 (104 + probe code 64).

## Gaps

- There is one session slot, and sessions longer than 8 chunks are not
  handled.
- Replay covers sound-event choices only. Other course systems do not draw
  through the log, so the replayed run is not a full deterministic game
  replay.
- There is no player-facing way to start a replay; only the self-test calls
  `replay_saved`.

## Course wiring

- `CourseSounds` now seals a full recorder through
  `RuntimeDecisionSessionStore::seal`. `seal` saves the chunk under the next
  index and continues recording.
- A failed save does not stop play. The manifest then names only the chunks
  that were written.
- The course self-test (code 169) fires 300 jumps, which seals one chunk. It
  checks that 300 decisions were logged and that the manifest names one
  chunk, then clears the store.
- Both course smokes pass.
- Negative control: making `seal` skip the save fails
  `character-course-smoke` with status 169.
- `CourseSounds::stop` also saves the partial live chunk when it holds
  decisions, so the whole session is on disk.
- The self-test then checks for a two-chunk manifest, and that chunk 1
  loads with at least 44 entries.
- Negative control: disabling the save in `stop` fails
  `character-course-smoke` with status 169.

## Course replay

- `RuntimeDecisionSessionStore::needs_roll` and `roll` handle both modes.
  - A full recorder is sealed to disk.
  - A replay that has consumed a full chunk loads chunk index + 1 through
    `RuntimeDecisionChunks::advance`.
  - If the next chunk is missing, the replay stays where it is, so its next
    draw reports the divergence.
- `CourseSounds::replay_saved` starts a replay from chunk 0 on disk.
  `replay_diverged_at` reports the tick of the first choice that differed.
- `stop` saves only while recording, so a replay never overwrites the
  session it plays.
- To stay under the 600-line limit, the bus-gain setters moved to
  `CourseSoundMix`.
- The course self-test runs three checks:
  - Code 230 records 300 jumps and saves them on stop.
  - Code 231 replays them from disk in a fresh `Sounds`. The variant hash
    must match, there must be no divergence, and 300 decisions must be
    logged, across both chunks.
  - Code 232 replays with the ticks shifted by one. The replay must report a
    divergence at tick 2.
- Both course smokes pass, and the source-length check passes.
- Negative control: pointing the roll at the wrong chunk fails
  `character-course-smoke` with status 231.
