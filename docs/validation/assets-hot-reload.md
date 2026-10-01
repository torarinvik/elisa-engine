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

## Bounded retention (2026-10-01)

`src/assets/hot_reload_policy.elisa` (`AssetsHotReloadPolicy`) holds the
float-free swap rules. `commit` swaps a slot only when `may_swap` allows it:
a staged generation, a ready dependency chain, and no retired generation
still held. A slot therefore keeps at most three generations resident (live,
retired, staged), and a retire with no consumers frees at once. `stage`
refuses a slot at `MAX_GENERATION` (2^40) with `Stage.Exhausted`.

`proof/hot_reload_policy.elisa` proves 42 obligations: a held retired
generation, a missing staged one or an unready chain each block the swap;
`resident` stays within 1..3; the next generation stays within the cap; and
an accepted slot indexes the 8-entry arrays. The test adds cases 14-18.

Negative controls: letting `may_swap` ignore the retired generation trips
its runtime postcondition (exit 134) and fails the proof; keeping a retired
generation when there are no consumers makes the test exit 17.

## Live renderer path (2026-10-01)

`src/runtime/render_scene_hot_reload.elisa` drives the table against the
native snapshot assets. Each slot owns an ID namespace, and generation g is
registered as `AssetId{high, low: g}`, so a row that names
`current(slot)` moves to the new generation on its next presenter sync,
which replaces the instance. `poll_mesh` and `poll_texture` compare the
file's size and write time (`elisa_render_scene_v1_asset_file_stamp`,
`native/render_scene_asset_watch_abi.inc`) and import an edited file as the
next generation. A texture edit restages every material that samples it,
and a material that fails to load marks the texture broken. `edit_material`
stages an edited descriptor. A broken import records `last_error` and
leaves the live generation drawn. `frame_boundary` unregisters released
generations (the native side refuses while anything still draws one), then
swaps.

`test/render_scene_hot_reload_native.elisa` (render smoke group 235) edits a
KTX2 texture, a mesh and a material, breaks the texture and the mesh, and
fixes both, checking the drawn mesh, material colour, sampled texture
generation and that swapped-out generations are unregistered. Negative
control: skipping the dependant restage on a texture edit fails group 235
at case 17.

Limits: the watcher reads mtime and size only; non-KTX2 loose images go
through Wicked's path-keyed resource cache, so only KTX2 textures reload
fresh; a texture with no dependent material is not validated before it
swaps; material edits are descriptors, not files.

## Gaps

Audio resources have no hot-reload path.
