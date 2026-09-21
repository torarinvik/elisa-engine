# Asynchronous snapshot assets

A04 requires that frame threads never block on file IO, and that unloading
during a read or after a failed dependency leaves nothing behind. The
render-scene snapshot loaders read cooked meshes and bundle textures on the
owner thread, inside registration. This slice adds a request path that moves
path resolution, the dependency check and the section read onto a worker
thread. The owner thread makes a finished load visible only when it pumps.
The native maze now loads its tile mesh and wall texture this way and keeps
presenting frames under a loading overlay while they load.

## Path

1. **Request.** These return without touching the filesystem:
   - `RenderScene::request_snapshot_mesh_asset(id, path)`
   - `RenderScene::request_snapshot_bundle_texture_asset(id, bundle, section)`

   A request records the ID, path and section, and queues a job under a new
   serial on `elisa::assets::SerialJobWorker` (`native/snapshot_asset_worker.h`).
   The worker is one thread, started by the first job, with at most 64 jobs
   queued, running or finished.
   - Requesting a resident ID again with the same path, or a loading ID with
     the same path and section, is a no-op.
   - A different path for either one is `InvalidValue`.
   - Requesting a failed ID again retries it.
   - Each kind reserves capacity at request time. Live slots plus live
     requests can't exceed the 32 meshes or 128 textures that synchronous
     registration allows.
2. **Load.** The job resolves the path under the project root, runs
   `verify_bundle_dependencies`, then calls `worker.checkpoint()`. After that
   it reads the mesh with the cooked-geometry loader or the texture section
   with `read_bundle_texture`. Only the job's own strings are captured. The
   job touches no service state and never takes the service mutex. The lock
   order is always service mutex, then worker mutex.
3. **Adopt.** `RenderScene::pump_snapshot_assets(budget)` takes up to `budget`
   finished results (1–64) in completion order. It matches each one to its
   request by serial and installs it with the same slot and byte-budget code
   as synchronous registration. It returns the number adopted. A failed load,
   or one that no longer fits a budget, marks its request failed and logs the
   reason. Pumping inside a snapshot transaction is `BatchActive`.
4. **Observe.** `RenderScene::snapshot_asset_state(kind, id)` returns
   `Absent`, `Loading` or `Resident`, or raises the failure (`AssetLoadFailure`
   or `Capacity`) until the ID is unregistered or requested again.
5. **Pending.** While a mesh is loading, a snapshot row that names it fails
   with the new `RenderSceneError.AssetPending`. A material that names a
   loading texture fails the same way. A failed row aborts the snapshot
   transaction as before, so the previous frame stays live. Synchronous
   registration of a loading ID is also `AssetPending`, and it replaces a
   failed request. No placeholder is substituted. The game chooses what to
   draw while it waits.
6. **Cancel.** Unregistering an ID that has no resident slot cancels its
   request:
   - A queued job never starts.
   - A job at its checkpoint returns without reading its section. A job
     already inside an OS read finishes that read first.
   - A running job's result is discarded when it returns.
   - A finished result is removed before any pump can take it.
7. **Shut down.** `RenderScene::shutdown` stops the worker: it drops queued
   jobs and finished results, releases a parked job and joins the thread.

The maze client (`examples/maze/native_client.elisa`) calls
`MazeRenderAssets::request` after `RenderScene::initialize`. It then pumps
application frames with a "Loading maze assets" overlay, calling
`MazeRenderAssets::advance` each frame. Once the mesh and texture are both
resident, `advance` registers the materials, because the wall material names
the texture. A failed load, or loading for more than 30 s of application time,
exits 17. That is the same code the packaged maze smoke already expects for a
bad bundle. Closing the window while loading exits 0. `MazeRenderAssets::register`
keeps the synchronous path for `test/maze_rendering_native.elisa`.

## Evidence

`test/render_scene_async_asset_native.elisa` runs inside the SDL3/Metal native
smoke, after the bundle-dependency and material-subset tests. It exits 194 and
logs its failing case through `elisa_render_scene_v1_test_failed_case`
("render scene test group 194 failed at case N"); see [Group code](#group-code).
It loads the bundles that `scripts/bundle_dependency_fixtures.py` writes to
`build/cooked/dependencies`. Test hooks built only into the smoke host
(`ELISA_RENDER_SCENE_TEST_PROBE`) can:
- hold the worker before a job starts, or at its checkpoint
- wait for exact started and finished job counts
- count live requests and worker jobs

That lets the test reach each cancellation point on purpose instead of by
timing.

| Cases | Checked |
| --- | --- |
| 1–10 | A mesh request returns OK, and repeating it coalesces into one job. The same ID with a texture bundle path is `InvalidValue`. With the job parked mid-load, the ID reads `Loading`, a snapshot row naming it and synchronous registration are both `AssetPending`, and four application frames run while no pump adopts anything. Releasing the job, one pump adopts it: the row stages and geometry bytes grow. A resident ID accepts only its own path. |
| 11–19 | `mesh-missing.elpk` (missing dependency) and an absent file fail on the worker, adopt nothing, read as `AssetLoadFailure` and allocate no bytes. Unregistering clears the failure. Requesting the failed ID again with a valid path retries it and it becomes resident. |
| 20–28 | Cancellation at each point: parked at the checkpoint, finished but not adopted, and still queued. Each leaves bytes unchanged and adopts nothing. The worker's started count shows the cancelled queued job never ran. |
| 29–36 | A bundle texture request: a material naming it is `AssetPending` while it loads and registers once it is resident, and retained texture bytes grow. A texture whose dependency is missing fails, and a material naming it is `AssetLoadFailure`. |
| 33 | A mesh and a texture finish in one pump. The mesh request reuses the request slot that a cancelled request freed, so a pump that matched results by slot order instead of serial would adopt the texture into the mesh request. Both become resident. |
| 37–39 | 40 requests with the worker held stop at exactly the free mesh slots, then cancel without starting. Unregistering everything returns geometry bytes, mesh slots and texture bytes to their baselines with no live request or worker job left. |
| 5 | The test ends with a mesh job parked mid-load. Application shutdown must release it and join the worker. |

The worker has its own probe, `native/snapshot_asset_worker_probe.h`. It
checks:
- completion order
- the 64-job bound, and rejection of serial 0 and of a serial that is still queued
- that a cancelled queued job never starts
- a job cancelled at its checkpoint: it sees the cancellation and its result is dropped
- removal of a cancelled finished result
- a default result from a throwing job
- `stop()` with one job parked and one queued: the parked job is released and discarded, the queued job never starts, and the thread joins
- a restart after `stop()`

The probe runs in three places:
- the Wicked probe
- the ASan/UBSan boundary harness (`native/boundary_harness.cpp`)
- a ThreadSanitizer build of `native/asset_worker_harness.cpp`, which
  `scripts/run_boundary_sanitized.py` now compiles and runs in the native
  gate with `halt_on_error=1`

`scripts/packaged_maze_smoke.py` is unchanged, and all nine cases pass
through the new path. The worker now reports each bad-bundle failure:
- missing bundle
- escaping symlink
- corrupted `mesh` or `wallalbedo` section
- missing texture bundle
- missing declared dependency

## Mutation checks

The render-scene mutations were applied to a copy of `native/`. The smoke's
C++ host was rebuilt from that copy, linked against the Elisa archive from the
full smoke, and run against the same fixtures. An unmutated control built the
same way exited 0. Every mutant failed this group at the listed case. These
runs predate group code 194. The group then exited `216 + case`, so each case
below is the recorded exit minus 216. Exits 220 and 226 were also
bundle-dependency exits, so those two mutants were rerun with the group code.
Each exited 194 and logged the case listed.

| Mutation | Failing case |
| --- | --- |
| the job skips its checkpoint | 6: the parked job completes, and a pump adopts it while frames should still be loading |
| a missing requested asset reads as `AssetLoadFailure` instead of `AssetPending` | 4 (rerun) |
| a loading request never coalesces | 2: the same ID with a texture path is accepted |
| unregistering a loading ID doesn't cancel its worker job | 27: the cancelled queued job starts |
| a resident mesh accepts any path | 10 (rerun) |
| requests ignore the per-kind slot bound | 39: all 40 fill requests are accepted |
| pump adopts each result into the first live request instead of matching serials | 33. Before case 33 existed this mutant survived, because worker cancellation had removed every stale result. |

The worker mutations were applied to a copy of `snapshot_asset_worker.h` and
run through the ThreadSanitizer harness. The unmutated control exited 0. Each
mutant exited 1 at the named check:

| Mutation | Failing check |
| --- | --- |
| `checkpoint()` ignores cancellation | drops a job cancelled at its checkpoint |
| a cancelled running job still posts its result | drops a job cancelled at its checkpoint |
| `cancel()` leaves a finished result | removes a cancelled finished result |
| `stop()` keeps queued jobs | stop releases, discards and joins a parked job |
| the queue admits 65 jobs | bounds its queue |
| the start hold is ignored | rejects repeated serials and never starts a cancelled job |
| a queued serial can be submitted twice | rejects repeated serials and never starts a cancelled job. The first probe draft only tried a repeated serial with the queue full, where the bound rejected it anyway, so this mutant survived until the check moved. |

## Limits

- **Cancellation granularity.** A job checks for cancellation once, between
  its dependency check and its section read. A cancelled read or decompression
  that has already started runs to completion, and its result is then
  discarded.
- **Adoption can still fail.** Requests count against the slot limits at
  request time, but synchronous registration of another ID doesn't count
  pending requests and can take the last slot first. The byte budgets
  (256 MiB of geometry, 256 MiB of encoded textures) are only known after the
  read. Adoption checks both and can fail with `Capacity`.
- **Owner-thread work remains.** Adoption moves the loaded geometry or
  encoded bytes into a slot. Wicked still decodes a bundle texture when a
  material that names it registers, and a mesh is uploaded when a snapshot
  row first uses it. Both happen on the owner thread.
- **No priorities or eviction.** Jobs run first in, first out on one thread.
  Nothing is evicted under budget pressure; the caller unregisters.
- **Path identity.** A resident or loading ID matches a later request by
  its path string, not its canonical file.
- **No placeholder.** A row that names a pending asset fails the frame's
  snapshot transaction. The game decides what to draw instead; the maze draws
  nothing but its overlay until both assets are resident.

## Validation on 2026-09-21

- `scripts/render_scene_native_smoke.py` passed on SDL3/Metal. That covers
  every case above, the earlier snapshot, bundle-texture and
  bundle-dependency tests, the maze snapshot test, the maze application smoke
  and all nine packaged maze cases. The three expected worker failures were
  logged: two missing dependencies and one absent file.
- `elisascript scripts/wicked_probe.elisascript build` rebuilt the probe with
  the worker check and passed its application and render smokes. The `frame`
  phase then passed.
- `scripts/run_boundary_sanitized.py` passed. The boundary harness reported
  `worker=1` under ASan/UBSan, and the ThreadSanitizer worker harness reported
  no finding.
- The seven render-scene mutations and seven worker mutations above failed as
  listed. Both controls exited 0.
- A syntax-only compile of `native/render_scene_abi.cpp` without
  `ELISA_RENDER_SCENE_TEST_PROBE` passed. Its three warnings predate this
  change.
- The full `PYTHON_BIN=/opt/homebrew/bin/python3 elisascript
  scripts/check.elisascript` suite passed at a load average near 90,
  including both Elisa Proof suites (17 and 6 obligations, none failed).
- `scripts/check_module_hygiene.py`, `scripts/check_source_length.py` and
  `git diff --check` passed.
- The sibling compiler checkout had uncommitted changes from other work.
  `ELISA_ALLOW_STALE_STAGE1=1` used its existing stage1 binary.
- That binary has no line continuation after `and` or `or`. The first draft of
  the native test wrapped two conditions and failed to parse. The test now
  names each part as a bool.

## Group code

The group first exited `216 + case`, so its exits 220–226 matched
bundle-dependency cases 20–26. It now exits 194, a code no other group in the
smoke returns, and logs its failing case. On 2026-09-21, with only that change
applied to a clean checkout:
- `scripts/render_scene_native_smoke.py` exited 0.
- The two mutants whose old exits were ambiguous exited 194. They logged
  "render scene test group 194 failed at case 4" and "... at case 10". Their
  control exited 0.
