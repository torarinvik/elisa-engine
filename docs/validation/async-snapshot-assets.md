# Asynchronous snapshot assets

A04 requires that frame threads never block on file IO, and that unloading
during a read or after a failed dependency leaves nothing behind. The async
snapshot path moves path resolution, dependency checks, section reads, and PNG
or JPEG decode onto a worker. The owner thread makes finished CPU data visible
when it pumps, then creates Wicked textures when materials bind them. The
native maze loads its tile mesh and wall texture this way and keeps presenting
frames under a loading overlay while they load.

## Path

1. **Request.** These return without touching the filesystem:
   - `RenderScene::request_snapshot_mesh_asset(id, path)`
   - `RenderScene::request_snapshot_bundle_texture_asset(id, bundle, section)`
   - `RenderScene::request_snapshot_mesh_asset_prioritized(id, path, priority)`
   - `RenderScene::request_snapshot_bundle_texture_asset_prioritized(id, bundle, section, priority)`

   A request records the ID, path and section, and queues a job under a new
   serial on `elisa::assets::SerialJobWorker` (`native/snapshot_asset_worker.h`).
   The worker is one thread, started by the first job, with at most 64 jobs
   queued, running or finished. The original methods use priority 0. The
   prioritized methods accept a signed 32-bit priority; larger values start
   first while jobs are queued. Equal priorities remain FIFO. Repeating a
   matching request updates its priority if it is still queued; running or
   finished work is unchanged.
   - Requesting a resident ID again with the same path is a no-op. A request
     for the same loading ID and path/section coalesces; the prioritized form
     updates the queued job's priority.
   - A different path for either one is `InvalidValue`.
   - Requesting a failed ID again retries it.
   - Each kind reserves capacity at request time. Live slots plus live
     requests can't exceed the 32 meshes or 128 textures that synchronous
     registration allows.
2. **Load and decode.** The job resolves the path under the project root,
   runs `verify_bundle_dependencies`, then calls `worker.checkpoint()`. After
   that it reads the mesh with the cooked-geometry loader or reads and decodes
   the PNG/JPEG section into bounded CPU pixels. The texture decoder verifies
   the dimensions against the checked image header and caps decoded storage at
   256 MiB per service. Only the job's own strings and owner-thread identity
   are captured. The job touches no service state and never takes the service
   mutex. The lock order is always service mutex, then worker mutex.
3. **Adopt.** `RenderScene::pump_snapshot_assets(budget)` takes up to `budget`
   finished results (1–64) in completion order. It matches each one to its
   request by serial and installs it with the same slot and byte-budget code
   as synchronous registration. Texture slots retain decoded pixels and
   account for both source-section bytes and decoded bytes. It returns the
   number adopted. A failed load, or one that no longer fits a budget, marks
   its request failed and logs the reason. Pumping inside a snapshot
   transaction is `BatchActive`.
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
| 29–36 | A bundle texture request: a material naming it is `AssetPending` while it loads; the worker decodes it, pump adopts it, and owner-thread material registration creates the GPU resource. Source and decoded-byte accounting grows. A texture whose dependency is missing fails, and a material naming it is `AssetLoadFailure`. |
| 33 | A mesh and a texture finish in one pump. The mesh request reuses the request slot that a cancelled request freed, so a pump that matched results by slot order instead of serial would adopt the texture into the mesh request. Both become resident. |
| 37–39 | 40 requests with the worker held stop at exactly the free mesh slots, then cancel without starting. |
| 41–57 | Geometry pressure keeps a mesh referenced by a committed row resident, evicts the older unreferenced mesh, reloads it, and then evicts the least-recently-used unreferenced mesh. Bytes remain within the configured budget and temporary rows and assets are released. |
| 58–70 | Source and decoded texture pressure keeps a material-referenced texture resident, admits two unreferenced textures, then evicts the least-recently-used one. Both byte counters remain within their configured budgets. |
| 71+ | Unregistering everything returns geometry bytes, mesh slots and texture bytes to their baselines with no live request or worker job left. |
| 5 | The test ends with a mesh job parked mid-load. Application shutdown must release it and join the worker. |

The worker has its own probe, `native/snapshot_asset_worker_probe.h`. It
checks:
- completion order
- descending priority order, FIFO ties, and reprioritizing queued work
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

- **Cancellation granularity.** `VirtualFileService` now reads ELPK sections
  in 64 KiB chunks and stops at the next chunk boundary after cancellation;
  the currently blocking chunk completes. Compressed sections stream through
  zstd in bounded 64 KiB input and output chunks, checking cancellation while
  decoding and discarding partial output on cancellation. The decoder caps its
  frame window at 64 MiB, matching the maximum unpacked section size. The
  production RenderScene snapshot worker passes the same checkpoints into mesh
  and texture section reads. PNG/JPEG image decode remains uninterruptible.
- **Adoption can still fail.** Requests count against the slot limits at
  request time, but synchronous registration of another ID doesn't count
  pending requests and can take the last slot first. The budgets (256 MiB of
  geometry, 256 MiB of source texture sections, and 256 MiB of decoded texture
  pixels) are only known after the read and decode. Adoption checks them and
  can fail with `Capacity`.
- **Owner-thread work remains.** Adoption moves geometry or decoded CPU
  pixels into a slot. Wicked creates the GPU texture, mip chain, and deferred
  block-compression request when a material registers; a mesh is uploaded
  when a snapshot row first uses it. Synchronous compatibility registration
  still reads and decodes bundle textures on its calling owner thread.
- **Priority and eviction.** Queued jobs use descending signed priorities, with
  FIFO order for ties; a running job cannot be preempted. Resident meshes and
  bundle textures use deterministic LRU eviction under their byte and slot
  budgets, but live scene rows and material registrations pin their assets.
  If the unreferenced set cannot satisfy an installation, adoption reports
  `Capacity` without partially evicting the resident set. The caller still
  unregisters assets explicitly when it knows they are no longer needed.
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
- The async asset test asserts that the resident bundle texture was decoded
  on a non-owner worker thread; its material creates and samples the texture
  on the owner thread. The truncated-image registration test now fails before
  retaining a texture slot.
- `VirtualFileService` cancellation validation observes 64 KiB of a 32 MiB
  section read, cancels before EOF, and verifies the result remains unpublished.
  A direct package-reader check cancels at the same boundary and confirms no
  partial section reaches its caller.
- The production asset-worker check pauses after the first 64 KiB read of a
  32 MiB bundle texture, cancels the job, and verifies no encoded texture is
  returned and the worker releases the job.
- The render-scene asset check lowers geometry and both bundle-texture budgets,
  verifies pinned rows/materials survive, and verifies least-recently-used
  unreferenced meshes and textures are evicted before adoption completes.
- `DEVELOPER_DIR=/Library/Developer/CommandLineTools PYTHON_BIN=/opt/homebrew/bin/python3 ELISA_COMPILER_BIN="/Users/torarinvikbjarko/Documents/Coding Projects/Elisa Projects/Elisa-compiler/scripts/elisac_stage1.sh" CXX=/opt/homebrew/opt/llvm/bin/clang++ elisascript scripts/wicked_probe.elisascript build` passed the native build and application/render smokes. The matching `frame` phase passed the new package cancellation probe and SDL3/Metal render checks.
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
- The stage1 seed was rebuilt after the macOS SDK update with
  `DEVELOPER_DIR=/Library/Developer/CommandLineTools`; it completed successfully.
  With that toolchain, the SDL3/Metal render smoke passed after adding priority
  submission. The maze requests its tile mesh at priority 100 and wall texture
  at priority 50. The worker probe also verifies queued reprioritization and
  priority ordering.

## Group code

The group first exited `216 + case`, so its exits 220–226 matched
bundle-dependency cases 20–26. It now exits 194, a code no other group in the
smoke returns, and logs its failing case. On 2026-09-21, with only that change
applied to a clean checkout:
- `scripts/render_scene_native_smoke.py` exited 0.
- The two mutants whose old exits were ambiguous exited 194. They logged
  "render scene test group 194 failed at case 4" and "... at case 10". Their
  control exited 0.

## Streaming decompression validation on 2026-09-22

- The package gate decodes a 4 MiB zstd section through bounded output chunks,
  cancels during decompression, and verifies the partial decoded buffer is
  discarded. Existing valid-section, checksum, truncated-frame, and declared
  size bomb checks still cover the streaming reader.
- `DEVELOPER_DIR=/Library/Developer/CommandLineTools PYTHON_BIN=/opt/homebrew/bin/python3 ELISA_COMPILER_BIN="/Users/torarinvikbjarko/Documents/Coding Projects/Elisa Projects/Elisa-compiler/scripts/elisac_stage1.sh" CXX=/opt/homebrew/opt/llvm/bin/clang++ elisascript scripts/wicked_probe.elisascript build` exited 0, including the package probe and SDL3/Metal render and packaged-maze smokes.
- A standalone ASan/UBSan build invoking `probe_zstd_streaming` exited 0, and
  the complete `package_bounds_probe.h` passed a C++17 syntax-only compile with
  the current Wicked and SDL3 headers.
