# A12 design — routing untrusted imports through workers

Status: design, 2026-10-03. Scope: the remaining A12 work in the
[implementation backlog](../../IMPLEMENTATION_PLAN.md): route editor and
catalogue imports through the import worker, and put pixel decoding under the
`AssetImportLimits` budget.

## Starting point

- `native/import_worker_process.h` runs a job in a forked child with a
  wall-clock deadline and an optional `RLIMIT_DATA` cap. It reports `imported`,
  `refused (reason)`, `crashed (signal N)`, `timed out` and
  `refused (import threw)`. Only `native/package_format_test.cpp` uses it.
- The live path, `src/runtime/render_scene_hot_reload.elisa`, imports in the
  render host process. `register_mesh` and `register_texture` call
  `RenderScene::register_snapshot_{mesh,texture}_asset(asset, path)`, which
  parse and decode the file in-process. A hostile or corrupt file can crash or
  hang the editor session.
- The catalogue path, `scripts/cook_assets.py` and `scripts/asset_cooks.py`,
  already cooks in subprocesses. Those subprocesses have no deadline and no
  memory limit, and their outcomes don't share the worker's diagnostic text.
- `GlbImport`, `GlbDocumentImport` and the package, cooked-model and texture
  header readers already charge the budget. Pixel decoding (PNG and JPEG
  bodies) doesn't charge it yet.

## Decision 1: exec a dedicated worker; don't run a lambda after fork

The render host is multithreaded: Wicked's job system and the audio and SDL
threads. After `fork()` in a multithreaded process, the child may only call
async-signal-safe functions until it calls `exec`. If a job allocates or takes
a lock held by another thread, it can deadlock. The current fork-and-call
design is therefore safe only in single-threaded tools and tests.

- Add `native/import_worker_main.cpp`, built as `elisa_import_worker`. Its
  arguments are the kind, source path, staging directory and limits. It
  imports through the same budgeted importers and writes one cooked artifact
  into the staging directory.
- The parent starts it with `posix_spawn`. The deadline, `SIGKILL`, `waitpid`
  and the reply framing stay as in `import_worker_process.h`. That framing is
  one status byte, `I` or `R`, followed by text. Add a `spawn` variant that
  shares `finish_import_worker`, so diagnostics stay identical. Keep the
  fork-based variant for single-threaded tests.
- The child never touches GPU or scene state. It returns only a staged file.

## Decision 2: the parent loads only validated cooked artifacts

On `Imported`, the reply payload is `<staged path>\t<sha256>\t<bytes>`. The
parent rechecks the size and hash, then calls the existing
`register_snapshot_*_asset` on the cooked artifact, which uses the bounded
loaders already fuzzed in A12. The original source file is never parsed
in-process. A staged file that fails to load is reported as `refused (staged
artifact invalid)` and handled like a broken import. The last good generation
stays live, as today.

## Decision 3: asynchronous, cancellable jobs polled at the frame boundary

New ABI, in a separate `render_scene_import_worker_abi.h` so
`render_scene_abi.h` stays under 600 lines:

```c
int64_t elisa_render_scene_v1_import_job_start(const char* path, uint32_t kind,
    const ElisaImportLimits* limits, uint32_t deadline_ms);
int32_t elisa_render_scene_v1_import_job_poll(int64_t job, ElisaImportJobResult* out);
int32_t elisa_render_scene_v1_import_job_cancel(int64_t job);
```

- `poll` doesn't block. It returns `Pending` or one terminal outcome exactly
  once. `cancel` is idempotent: it kills the child, reaps it, removes the
  staging files and makes the job's next poll return `Cancelled`.
- The `Reloader` gains a `Pending` state per slot. A changed file starts a job,
  and the slot stages only when the job reports `Imported` at a frame
  boundary. A further edit while a job is pending cancels it and starts a new
  one, so only the newest content can stage.
- Capacity: a fixed pool of 2 concurrent workers and a queue of 32 jobs. When
  the queue is full, the request is refused as `TooMany`; nothing is dropped
  silently. Jobs for the same path and content hash share one worker.
- Shutdown cancels every job before the render scene is destroyed. A test
  covers this ordering.

## Decision 4: the catalogue uses the same contract

`asset_cooks.run_command` gains `timeout=` and a `preexec_fn` that sets
`RLIMIT_AS`/`RLIMIT_DATA` on POSIX. It maps outcomes to the same five
diagnostic strings. The catalogue row and the cook cache are written only
after `imported`, inside the existing SQLite transaction. A failed cook keeps
the last good outputs; the test for that already exists.

## Decision 5: pixel decoding under the budget

Before decoding, the decoder charges `width * height * 4` through
`charge_alloc`, using the overflow-safe division check. It charges
decompression work per output row through `charge_work`. JPEG progressive
scans count as work. A refused charge leaves no partial image.

## Reproducible diagnostics

Each result records the input sha256, the limits, the deadline, the outcome and
its diagnostic line, so the same file and limits give the same line. The
seeded fuzz corpora (GLB, cooked model, package, texture) gain a mode that
replays every mutant through `elisa_import_worker`. The mode asserts that no
mutant crashes the parent, that the catalogue and session are byte-identical
afterwards, and that each outcome is stable across two runs.

## Proof

`proof/import_job.elisa` covers the pure job state machine:

- `Pending` reaches exactly one terminal state.
- `cancel` is idempotent and terminal.
- Only `Imported` can stage.
- Pool and queue counts stay within capacity.
- A superseded job can never stage.

The staging rule reuses `hot_reload_policy`.

## Slices, in order

1. Spawned worker executable and the `spawn` variant, with the existing outcome
   tests run against it. ASan+UBSan stays clean.
2. Pixel-decode budget, with a negative control that drops the dimension
   charge.
3. Asynchronous job ABI, the `Reloader` `Pending` state, and the proof.
   Render smoke: a hanging texture edit times out while frames keep
   presenting, and a crashing mesh keeps the last good generation.
4. Catalogue timeout and memory limit, and corpus replay through the worker.

A12 is done when corrupt, hanging and crashing inputs produce these
diagnostics in both the editor and catalogue paths, and leave the catalogue
and editor session unchanged.

## Out of scope

Windows workers (`CreateProcess` plus a Job Object) are a separate task;
until then, Windows imports stay in-process and are marked hardware-unverified.
