# Character course pointer menu

Validated on 2026-09-28 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler (`ELISA_ALLOW_STALE_STAGE1=1`), SDL3/Metal, Wicked and Jolt.

This is delivery-queue item 5 (I01 pointer input, I02 hit testing) in the
second game. It extends [`course-accessibility.md`](course-accessibility.md).

## Design

- **Hit test.** `legend_hit` in `examples/character_course/play.inc` maps a
  pointer in logical window units to a legend row, or -1.
  - A row spans the label and key columns and one scaled row height from its
    top.
  - It uses the same `legend_row_y` and `legend_key_offset` as the layout, so
    clicks follow the text at 100, 125 and 150%.
- **Menu presses.** `CourseControls::menu_pointer` handles presses:
  - A left press on a key slot selects it; the next key press rebinds it as
    before.
  - A left or right press on a settings row selects the row and steps it
    forward or back. This is the same `Adjusted` result as Left/Right.
  - Middle presses, releases, motion and presses outside the legend are
    ignored.
- **Wheel.** `menu_wheel` moves the selection up or down, like the arrows.
- **Queue ownership.** While the menu is open, keyboard and gamepad events
  are drained first. Pointer events are drained only when no key acted, and
  that drain stops at `Adjusted` so each change is laid out before the next.
  In gameplay the course has no pointer controls. `discard_pointer` empties
  the pointer queue every frame, so a click never waits for the menu and the
  queue never overflows.

## Checks

Codes 162–165 run in the course self-test after the accessibility checks.

| Code | Check |
|---|---|
| 162 | the middle and last pixel of every row hit that row at 100, 125 and 150%. Points left of, right of, above and below the rows miss. The 100% position of the last row lands on a different row at 150% |
| 163 | a left press selects a key slot and a right press there is ignored. Left and right on settings rows adjust +1 and -1 and select that row. Middle presses and misses are ignored |
| 164 | at 125% a click on Text size gives 150%. Release, motion and an outside click are ignored. Wheel up wraps to Captions and wheel down selects the next row |
| 165 | with nothing queued, both pointer drains are no-ops |

The events in 162–165 are built in Elisa. The SDL pointer queue itself
(button, motion and wheel coordinates in logical units) is covered by the
RenderScene selection smoke. The course is an ordinary project, so it does not
link the native test-push probe. No physical mouse run is recorded here.

Commands:

```
PYTHONPATH=scripts python3.14 scripts/application_native_smoke.py --only character-course-smoke,character-course-relaunch-smoke
elisascript scripts/check.elisascript
elisascript scripts/native_gate.elisascript native
```

The self-test, both smokes (`smoke=0`), the full check (`check=0`, on a rerun
after a transient 126) and the native gate (`gate=0`) pass.

## Negative controls

- **The hit row height is not scaled.** The self-test fails at 162. An
  earlier version checked only row middles and passed this control, so the
  check now also probes each row's last pixel.
- **A right click steps settings forward.** The self-test fails at 163.
- **Releases act as presses.** The self-test fails at 164.

All controls were reverted.

## Gaps

- There is no hover highlight or pointer cursor feedback, and no pointer
  gameplay (camera look).
- The hit test assumes that overlay text positions are top-left in logical
  units, as in the layout. The canvas maps those units to physical pixels by
  the display DPI ([`overlay-dpi-scaling.md`](overlay-dpi-scaling.md)).
- There are no drag or scroll containers; the legend fits on screen at every
  size.
