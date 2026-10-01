# Per-cell asset ownership

`CellAssets::Ledger` (`src/world/cell_assets.elisa`) connects resident scene cells to the A04 `AssetLoader` request lifecycle. Each cell declares a `Manifest` of up to four stable asset keys (`AssetId` plus artifact variant) with estimated byte sizes. One loader request backs each key, and every cell that lists the key holds a reference to it.

- **Admission is all-or-nothing.** `acquire` computes the bytes the manifest would add beyond already shared keys. It rejects duplicate keys within one manifest, and `CellAssetBudget::admits` rejects any manifest whose bytes would take the committed total over the declared budget. If a loader request fails partway (capacity), every reference taken so far is dropped and the new requests are cancelled.
- **Release follows ownership.** When the last owning cell unloads, a resident request is released, and a request that is still queued, decoding or uploading is cancelled. Shared keys stay live while any owner remains.
- **Budgets.** The ledger hands its declared budget to the loader as well, so admitted resident bytes never trigger loader eviction of a referenced resource. A decode that reports more bytes than reserved is accepted only if `CellAssetBudget::resized` keeps the total within budget.
- **World ordering.** `CellAssetWorld` (`src/world/cell_asset_world.elisa`) acquires a cell's assets before creating its `CellWorld` anchor/prefab and rolls them back if world activation fails. On unload or `trim` it destroys the world payload first and releases the assets after that, so entities never outlive the resources they reference.

## Evidence

`test/cell_assets.elisa` (run by `scripts/check.elisascript`) covers:

- one request shared by two cells (refcount 2), and repeat activation rejected
- rejection of a duplicate-key manifest
- budget rejection with no request issued and no world change
- world-side rejection (stream budget full) rolling back an admitted acquire, with the new request cancelled
- a decode larger than the budget refused without changing committed bytes
- unloading one of two owners releasing only the unshared asset, while the shared one stays resident and valid for the other cell
- cancellation of an asset in mid-decode and in mid-upload when its cell unloads, with later stage reports for the stale key refused
- trim releasing the last references, leaving zero committed and zero resident bytes
- 78 travel steps walking across 13 cells and back three times. Each step trims with hysteresis, streams in the player's cell (one shared and one cell-unique asset) and checks that the cell is fully resident, committed and resident bytes are within the 80-byte budget, there are no loader evictions, `CellAssetWorld::valid` holds, and at most four requests are live. Teardown leaves nothing live.

Mutation check (scratch copies, all killed): forcing the last-owner path, removing acquire rollback, removing budget admission, removing mid-load cancellation, removing asset rollback on world failure, disabling the decode budget check, accepting duplicate keys and ignoring unload ordering failures.

`proof/cell_asset_budget.elisa` proves `CellAssetBudget`: admission implies `committed + extra <= budget`, release never goes negative or grows, a resize stays non-negative, and per-cell slot indices stay below capacity. The prover could not combine three-term sums such as `committed - reserved + decoded`, so `resized` composes the proved `released` and `grown` helpers.

## Native streaming smoke (2026-10-01)

`cell-streaming-smoke` (`test/cell_streaming_native_main.elisa`, run by `scripts/application_native_smoke.py`) streams cells on a live, hidden SDL3/Wicked scene (macOS 27.0, Apple M5, Metal, stage1 with `ELISA_ALLOW_STALE_STAGE1=1`).

- A player walks x = 0..12 and back three times (78 steps). Each step unloads cells outside the hysteresis ring in a fixed order: renderer row, then the `CellWorld` payload, then the `CellAssets` references. It then streams in the player's cell. Its manifest has a mesh key shared by every other cell and a material key unique to the cell. Each stage report (`begin_decode`, `complete_decode`, `upload_complete`) is separated by a real frame pump.
- `CellVisuals::resolve` (`src/world/cell_visuals.elisa`) rebuilds the cell's renderer row from its stable keys through `VisualShelf` (shape from the mesh, colour from the material). It does this only when the cell owns both keys (new `CellAssets::cell_holds`) and both requests are resident. `test/cell_visuals.elisa` covers queued, half-loaded, foreign-cell, swapped-kind, unknown-to-shelf and released cases. Mutants that drop the ownership check, the residency check, the shape check, or the `cell_holds` slot comparison fail it with codes 13, 7, 22 and 21.
- After every step: each live row is backed by owned resident assets and still probes in the renderer; the renderer instance count equals baseline plus live cells (at most 3); `CellAssetWorld::valid` holds; committed and resident bytes are within the 100-byte declared asset budget; and there are no loader evictions.
- Memory: the new `elisa_application_v1_memory_usage` ABI (`ApplicationMemory::sample`) queries the device's live GPU allocation and budget and the process's `phys_footprint` on every call. The first pass (26 steps) records warm peaks, and every later sample must stay within the declared allowances (GPU +8 MiB, CPU +16 MiB, `CellMemoryBudget::within`) and under the device budget. Measured: GPU usage never rose above the warm peak (a zero GPU allowance still passes); CPU growth was under 1 MiB but not zero (a zero CPU allowance fails with 76). The warm footprint was about 640 MiB, and GPU about 200 MiB or more, mostly a 256 MB suballocation block.
- Controls (all killed): skipping the row destroy on unload exits 67; skipping the material stage exits 62; a zero CPU allowance exits 76. After teardown the instance count returns to baseline, and no assets, committed bytes or resident bytes remain.

`proof/cell_memory_budget.elisa` proves `within` (a passing sample is non-negative and at most baseline plus allowance), that `peak` is monotone, and that `row_in_use` stays below capacity.

## Native stages (2026-10-01)

- `native/application_asset_stream_exports.inc` exposes `native/native_resource_loader.h` through the application ABI (mount, request, pump, query, drop, live, reset), wrapped by `src/runtime/asset_stream.elisa`.
- `CellAssets::report_native` takes a native stage (queued, decoding, uploading, ready, failed, cancelled) and byte count by asset key and catches the portable ledger up. It refuses unknown keys and reports that fall behind the ledger, and it marks the asset failed when the native request gives up. `cell_ready_count` and `cell_failed` let a cell tell when all its assets are ready. The stage arithmetic lives in `src/world/cell_asset_stages.elisa`, is proved in `proof/cell_asset_stages.elisa` and is tested by `test/cell_asset_stages.elisa`.
- `cell-streaming-smoke` writes cooked mesh packages (box, pyramid; shared by two cells each) and one material package per cell. It requests them natively per key, pumps real decode and upload completions into `CellAssets` and checks each uploaded texel. It drops native requests by refcount on unload and requires the native live count to equal the ledger after every step and to reach zero at teardown.

## Streamed meshes and bound materials (2026-10-01)

- `RenderScene::create_streamed_mesh` (`src/runtime/render_scene_streamed.elisa`, `native/render_scene_streamed_mesh_abi.inc`) builds a row from the cooked mesh bytes the native stream already decoded (copied out under the loader lock by `elisa_asset_stream_copy_bytes`, `native/asset_stream_borrow.h`); nothing is reread from disk. The material stream's uploaded texture is shared into the row's Wicked material as its base-colour map, with a white base colour.
- `RenderScene::streamed_material_bound` checks that the row's base map is the very texture the stream keeps resident. `cell-streaming-smoke` requires it for every live row after every step (code 84) and passes.

## Character course crossing cells (2026-10-01)

`examples/character_course` now streams a 15 by 7 grid of floor cells (`cells.elisa`, built by `make_cells.py`) around the real player character. Each frame the player's physics position goes through `CellAssetWorld`, `CellVisuals` and the native asset stream. Each resident cell draws a streamed mesh with its streamed texture bound. Every step checks that the referenced rows, meshes and materials are still valid. GPU and process memory are checked against the 8 MiB and 16 MiB growth allowances after a warm pass.

`character-course-cells-smoke` builds `stream_test_main.elisa` (mode 3). It drives the game's own input path by pushing SDL key events (`native/application_test_input_exports.inc`): the player runs six legs between the course ends and turns at each one. The smoke passes only with these results:
- at least 24 cell crossings
- at least 12 native loads
- no reference or budget violations
- a clean close (codes 101 to 104 and 60 to 112 name each failure)

The play loop's stack frame stays small. The cell World, the cell runtime and `Cells` are created once in `run_mode_cells` and passed to the loop by `mutable &`. Beacons are created in `run_after_open`, so their 2 MB create frame is never stacked on top of the loop. With this layout the course runs on the default 8 MB main-thread stack; no stack-size linker flag is used.

## Remaining

- The streamed floor tiles are generated stand-ins (`make_cells.py`), not authored course content.
- Cell prefabs carry no gameplay colliders; the player walks on the course's own floor.
