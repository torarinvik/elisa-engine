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

## Remaining

This is the portable ownership contract. The native loader (`native/native_resource_loader.h`) still reports its stages through its own A04 path. Feeding native decode/upload completions into `CellAssets` by stable key, and rebuilding renderer mesh/material resources for a cell's prefab from those keys, are not wired. No shipped game streams cells yet. As a result the W05 Done criterion (a player crossing cell boundaries in a running game, within measured CPU/GPU budgets) is met only at the portable-test level.
