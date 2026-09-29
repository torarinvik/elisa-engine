# Asset hot-reload policy

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the Stage1 compiler.
This is the first A10 progress.

## Design

`src/assets/hot_reload.elisa` (`AssetsHotReload`) tracks generations for up
to 8 assets with one dependency each (`parent`).

- `stage` records an edited asset as the next generation. A broken import
  is counted as an error, clears any staged generation and never touches
  the live one.
- `commit` runs at a safe frame boundary. It decides every swap first, then
  applies them, so a frame never sees half a batch. An asset whose parent
  is left broken is held back, so a mesh never swaps onto a material that
  failed to import.
- The replaced generation is retired with a consumer count; `release`
  frees it only when the last in-flight frame or voice lets go.

## Checks

`test/assets_hot_reload.elisa` exits 0: staging is invisible until commit;
the old texture outlives two in-flight frames and is freed exactly once; a
broken material keeps the last good one and holds back its dependent mesh
while an unrelated sound swaps; and fixing the material swaps both in one
commit. Negative control: ignoring broken parents makes the test exit 9.

## Transitive dependencies (2026-09-29)

`ready` now walks the whole `parent` chain, bounded by the slot count, so a
broken texture holds back a mesh two links away, and a cyclic table never
swaps instead of looping. The test adds both cases (codes 12 and 13).
Negative control: checking only the direct parent makes the test exit 12.

## Gaps

There is no file watcher, and nothing reaches the renderer or audio yet, so A10 stays open.
