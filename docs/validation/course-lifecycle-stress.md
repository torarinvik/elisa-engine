# Character course lifecycle stress

Validated on 2026-09-28 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler (`ELISA_ALLOW_STALE_STAGE1=1`), SDL3/Metal, Wicked and Jolt.

This is Q06a progress in the second game. It extends
[`course-checkpoints.md`](course-checkpoints.md) and
[`course-durable-beacons.md`](course-durable-beacons.md).

## Design

`examples/character_course/stress_test.inc` runs at the end of the course
self-test.

- **Iterations.** It runs a bounded 12 iterations. Each one:
  - restarts the character and checks that the old handle is stale and the
    new one is at the start;
  - resizes the real SDL window to 240×150, 720×450 or 384×240 through the
    `elisa_application_v1_test_set_window_size` test-probe hook, pumps a
    frame, and checks that the frame reports that logical size and a newer
    resize serial;
  - resizes the view to 640, 1920 or 1024 pixels wide (16:10);
  - builds the beacon World and its renders, lights one beacon and releases
    everything;
  - starts sound, fires a jump, pauses and resumes the Effects bus with the
    matching mix moods, stops, and checks that no voice or stream is live;
  - pumps one frame.
- **Counts.** Iteration 0 is warm-up. Its counts become the baseline: render
  instances, live navmeshes, audio voices and audio streams. Every later
  iteration must end on exactly that baseline.
- **Trace.** A drift returns 180 plus the iteration, so the exit code names
  the first iteration that leaked. There is no text log.
- **Beacon saves.** The stress test never writes a beacon save. The relaunch
  smoke still reads the handoff that `beacons_test` wrote.
- **Injected failures.** These run after the view is restored to its original
  size.
  - `RenderScene::resize` rejects a zero width and a negative height, and
    accepts the original size.
  - `CourseSounds::start_injected` swaps one real path for a missing file.
    - A missing ambience clip gives `CLIP_FAILED`.
    - A missing music stream gives `MUSIC_FAILED`.

    In both cases `stop` releases every clip loaded before the failure and the
    audio service goes idle. A normal sound cycle works afterwards.
  - Destroying the already-destroyed character handle is rejected.
- **Heap sample.** `RenderScene::heap_bytes_in_use` reads the process heap
  (`malloc_zone_statistics` over all zones on macOS; zero elsewhere). The
  test samples it after iteration 3 and again after iteration 11. Growth over
  those eight iterations must stay within 256 KiB. On 2026-10-03 it measured
  under 16 KiB. A zero sample fails, because the gate is macOS-only.
- **Final count.** It must still equal the baseline.

## Checks

| Code | Check |
|---|---|
| 172 | restart leaves the old character handle stale and puts the new one at the start |
| 173 | the beacon World builds, lights exactly one beacon and releases |
| 174 | sound starts, plays, pauses, resumes, stops and goes idle |
| 175 | a lifecycle call raised during an iteration |
| 176 | invalid resizes are rejected and the original size is accepted |
| 177 | an injected clip failure is reported and fully released |
| 178 | an injected stream failure is reported and fully released, and sound works again |
| 179 | a stale character destroy is rejected |
| 181–191 | the resource counts after iteration 1–11 differ from the warm-up baseline |
| 195 | an OS window resize did not report its size and a new resize serial |
| 196 | restoring the original OS window size failed |
| 192 | the counts after the injected failures differ from the baseline |
| 193 | heap growth from iteration 3 to 11 exceeds 256 KiB |
| 194 | the heap sampler returned zero |

Commands:

```
PYTHONPATH=scripts python3.14 scripts/application_native_smoke.py --only character-course-smoke,character-course-relaunch-smoke
elisascript scripts/check.elisascript
elisascript scripts/native_gate.elisascript native
```

On 2026-09-28 the course self-test exited 0 under lldb, and the course and
relaunch smokes, `check` and the native gate each exited 0.

## Negative controls

- **Iteration 5 skips the beacon release.** The self-test fails at 185, which
  names that iteration.
- **A failed sound start leaves the sounds not live.** `stop` then returns
  early and keeps the loaded clips. The self-test fails at 178: after two
  leaked starts the 16 clip slots cannot hold a full start. Clip leaks are
  detected only through this capacity, because `AudioRuntime` has no clip
  count.

- **The resize check expects width + 1.** The self-test fails at 195
  (2026-10-03).
- **The heap limit is 1 byte.** The self-test fails at 193 (2026-10-03).

All controls were reverted.

## Gaps

- The counts come from public APIs only: render instances, navmeshes, audio
  voices and streams, plus stale-handle checks for characters and beacon
  entities, plus a process heap sample. GPU memory and Jolt body counts are
  not sampled.
- The run is 12 iterations inside the finite self-test, not a multi-hour soak
  (Q06). There is no ASan/UBSan run of the course binary itself.
- The OS window is resized through SDL on a hidden window, so no user drag
  or display change is driven.
- The trace is an exit code, not a retained log.
