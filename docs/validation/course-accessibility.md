# Character course accessibility settings

Validated on 2026-09-28 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler (`ELISA_ALLOW_STALE_STAGE1=1`), SDL3/Metal, Wicked and Jolt.

This is delivery-queue item 5 (I06 and I03) in the second game. It extends
[`course-controls.md`](course-controls.md) and
[`course-durable-beacons.md`](course-durable-beacons.md).

## Design

- **Settings.** `examples/character_course/access.elisa` (`CourseAccess`)
  holds three settings:
  - text scale: 100, 125 or 150%
  - high contrast: on or off
  - captions: on or off

  `adjust` clamps the scale and toggles the two flags. All three default to
  the plain layout.
- **Menu.** The pause-menu rebinding screen (`CourseControls::Menu`) now has
  nine rows: the six key slots, then Text size, High contrast and Captions.
  - Up/Down and the D-pad wrap over all nine rows.
  - Left/Right (or D-pad Left/Right) on a settings row returns `Adjusted`
    with a `delta` of ±1. On a key slot they do nothing.
  - Letter keys on a settings row are ignored, so they cannot rebind anything
    by accident.
  - `drain_menu` stops at `Adjusted` as well as `Closed`, so every change is
    applied before the next event is read.

  The three settings can be changed with a keyboard or a gamepad alone;
  rebinding a key slot still needs a keyboard.
- **Scaled HUD.** The instruction line, the legend rows, the key column and
  the caption line all scale with the text setting. Row positions and the key
  column's offset scale by the same factor as the font.
- **Contrast.** High contrast shows an opaque black panel behind the legend
  (created first, so text draws over it). It also switches the header to pure
  white and the selection and captions to pure yellow.
- **Captions.** Every sound event the game fires (jump, landing, win, fall,
  and the beacon or save chime) sets a caption such as "[Sound: jump]". The
  caption shows for 2.5 s of `frame.elapsed_nanos`. Captions follow the
  events, not the audio device, so they still appear on the silent fallback
  route. The overlay text changes only when the caption changes.
- **Persistence.** Settings are saved as the versioned `course-access`
  user-data record when the menu closes, alongside the keymap. At startup
  they are loaded and applied before the first frame.

  The loader rejects any of the following and restores the defaults with an
  in-game message:
  - a wrong version
  - a short record
  - a scale off the 25% steps or out of range
  - a flag that is not 0 or 1

## Checks

Codes 153–159 run in the course self-test. Codes 160–161 run in the relaunch
process (`character-course-relaunch-smoke`).

| Code | Check |
|---|---|
| 153 | adjust: steps and clamps the scale, toggles each flag alone, ignores unknown rows |
| 154 | a 150% font is 30 px from 20 px, and the key column moves right |
| 155 | menu: Up from the first slot wraps to Captions and Down wraps back; Right/D-pad Left adjust ±1; a letter key on a settings row and Right on a key slot change nothing |
| 156 | save and load round-trip |
| 157 | wrong version, off-step scale, out-of-range scale, flag 2 and a 3-field record are all rejected to defaults; a removed record reads as missing |
| 158 | captions: hidden when off, shown while on, expire after 2.5 s, and the newest event replaces the old one |
| 159 | measured layout: after the glyphs are ready, the widest of the nine labels ends before the scaled key column at 100, 125 and 150% |
| (159) | the live HUD then applies large and plain settings, highlights a settings row, and releases the panel and every text handle |
| 160 | relaunch: the 150% / contrast / captions settings come back and build a legend |
| 161 | relaunch: the record is removed afterwards |

Commands:

```
PYTHONPATH=scripts python3.14 scripts/application_native_smoke.py --only character-course-smoke,character-course-relaunch-smoke
elisascript scripts/check.elisascript
elisascript scripts/native_gate.elisascript native
```

Both smokes pass, and so do the full check (`check=0`) and the native gate
(`gate=0`).

## Negative controls

- **The decoder accepts any non-negative flag.** The self-test fails at 157.
- **The key column is not scaled.** The self-test fails at 154.
- **The key column is scaled from 60 px, too narrow for the labels.** Only
  the measured layout check catches this; the self-test fails at 159.

All controls were reverted.

## Compiler notes

- `legend.labels[row] <- try create_overlay_text(...)` inside a loop that
  captured a `mutable Legend` local lost every element write. The handles
  read back as 0 and the first `set_overlay_text_size` raised. The handles
  are now gathered in local arrays and the `Legend` is built from them
  afterwards. The loops that read the arrays copy them out of the forwarded
  `Legend&` first.
- The first measurement pass must request every label before pumping.
  Returning at the first not-ready label left the others unrequested, so
  they never became ready.

## Gaps

- There is no DPI or content-scale factor yet. The text setting is the only
  scale, and the canvas is logical pixels (I02).
- No reduced-motion setting, no gamepad rebinding, and no semantic control
  metadata or platform accessibility bridge (I06).
- The contrast theme covers the HUD, not the 3D scene.
- No display or audio-device selection (I03). Physical controller behaviour
  is covered only by mapping; no hardware run.
