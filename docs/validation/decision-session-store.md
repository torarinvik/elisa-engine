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
- The course's `CourseSounds` still discards its sealed chunks instead of
  calling `save_chunk`.
- No course session has been replayed from disk.
