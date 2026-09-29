# Streamed navigation tiles

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler (`ELISA_ALLOW_STALE_STAGE1=1`).

This is N06 progress. It is the generation and rebake policy only; no
DetourTileCache or Recast tile data is connected.

## Design

`src/nav/stream.elisa` (`NavStream`) tracks 8 tiles. Load, unload, obstacle
`mark_dirty` and rebake each bump a tile's generation. A `Request` records up
to four tiles and the generation seen for each, and `usable` is true only while
every tile is loaded, clean and unchanged. A route computed across a tile that
was unloaded, reloaded or rebuilt meanwhile is therefore never usable, and an
obstacle makes it stale before the rebuild runs. Dirty marks on unloaded tiles
are ignored. `rebake` rebuilds at most two dirty tiles per frame, lowest index
first, so the rest wait; a fresh request over a rebuilt tile is usable again.

## Checks

- `test/nav_stream.elisa` exits 0 (codes 1–18).
- Negative control: dropping the generation bumps on load/unload and loosening
  the comparison makes it exit 7.
- Source-length check passes.

## Gaps

- No agent recovery behaviour, no Detour query integration and no measured
  rebake cost. No proof harness. Full gate still blocked at `world-test`.
