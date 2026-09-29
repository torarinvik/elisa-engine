# Pipeline cache keys and warm-up

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler (`ELISA_ALLOW_STALE_STAGE1=1`).

This is R13 progress. It is the cache-key and warm-up policy only; no Wicked
pipeline is compiled or persisted through it.

## Design

`src/backend/pipeline_cache.elisa` (`BackendPipelineCache`) keys a pipeline by
shader package version, permutation, backend family and driver id. Entries are
compared field by field (no hashing, so no collisions), so a change to any one
field misses the old entry.

- `acquire` is the draw path: a warm hit is counted as `warm_hits`; a miss is
  counted as `cold_misses` and queues the pipeline, so cold and warm hitches
  are attributed separately. Invalid keys are ignored, not counted.
- `request` deduplicates against both the cache and the queue; the queue holds
  8. `warm_step(budget)` compiles at most `budget` queued pipelines per frame,
  oldest first, into a free slot or by round-robin eviction of the 8-entry
  cache.
- `invalidate_before(version)` drops entries from older shader versions.

## Checks

- `test/backend_pipeline_cache.elisa` exits 0 (codes 1–14).
- Negative control: leaving the driver id out of the comparison makes it exit 5.
- Source-length check passes.

## Gaps

- No persisted on-disk cache, no offline preparation, and no measured
  cold/warm hitches on a real frame, so R13's done condition is not met. Full
  gate still blocked at `world-test`.
