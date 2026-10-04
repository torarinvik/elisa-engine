# Sound asset hot reload

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler (`ELISA_ALLOW_STALE_STAGE1=1`).

This is S05 progress. [`music-transitions.md`](music-transitions.md) listed hot
reload as a gap.

## Design

- **Policy.** `AudioEvents::carry_over(next, previous)` moves what must survive
  a reload from the old board to a board built from the changed asset.
  Events match by id. A surviving event keeps its firing history
  (`fired`, `last_tick`), its variant rotation and its live instances, with
  each instance keeping its slot and start order. An event the new asset
  removes loses its instances; the caller stops the voices in slots live before
  and not after. A reloaded tick therefore cannot fire twice, and playing
  voices are not restarted. Weights and seed come from the new asset.
- **Course.** `CourseSounds::reload_events(sounds, path)` reads the asset,
  checksums it and returns `UNCHANGED` for the same bytes. Otherwise it builds
  a fresh board and triggers with `define_from` (all or nothing), carries over
  the old board, stops removed voices and swaps both in. An unreadable or
  invalid file returns `REJECTED` and leaves the loaded asset untouched.
  `poll_reload` runs every frame from `play_loop.inc`, re-reading
  `sounds/events.sfx` at most every 500 ms, so an edit to the file takes effect
  in the running course without a restart. The footstep and landing trackers
  restart from the next pose so a reload cannot register a spurious landing.
- Nothing native changed: the poll uses the existing bounded
  `AudioRuntime::read_asset`. The asset is under 4 KiB, so a poll reads and
  checksums it in negligible time.

## Checks

- `test/audio_events.elisa` exits 0. Codes 70–78 cover `carry_over`: live
  instances kept, history and rotation kept (duplicate and cooldown still
  apply), removed events dropped.
- Course self-test 198 (no audio): the same bytes reload nothing, an altered
  asset swaps in a deeper pause duck, an invalid file and a missing file are
  rejected without change, and the original reloads.
- Course self-test 199 (live audio): after a reload the jump's tick is still a
  duplicate, the removed footstep voice is stopped and its event is unknown,
  reinstating the asset restores it, and polling is throttled and idle on
  unchanged bytes.
- The fixtures are `sounds/events_alt.sfx` (no footstep, deeper pause duck)
  and `sounds/events_bad.sfx` (rejected as a whole).

## Gaps

- Only the sound asset reloads; the clip files themselves and the streamed
  music files do not.
- A reload replaces movement-trigger accumulators (stride distance) with the
  new asset's fresh state.
- The change is detected by re-reading the bytes, not by a file watcher.
- Animation-event triggers remain for S05.
