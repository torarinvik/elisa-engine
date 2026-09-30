# Ground-contact spans

`FootContact` (src/animation/foot_contact.elisa) finds where a foot joint is on the ground. The viewport draws M05's contact markers from these spans.

- `frame_speeds` gives the speed at each frame from the step to the previous frame. Frame 0 uses the step to frame 1.
- `spans(..., up, fps, limits)` returns `[first, last]` frame pairs:
  - A contact starts when the height along `up` is at most `enter_height` and the speed is at most `enter_speed`.
  - It holds until the height passes `exit_height` or the speed passes `exit_speed`, unless the enter test passes again on that frame.
  - Spans shorter than `min_frames` are dropped. A contact still held at the last frame closes there.
  - NaN compares false, so it breaks a contact and cannot start one.
- `markers` gives the position at each span's touch-down frame.

`ContactIndex` (src/animation/contact_index.elisa) holds the hysteresis step and the span keep rule. Its proof (proof/contact_index.elisa) is 44/44 with every obligation replayed.

## Validation

test/animation_foot_contact.elisa runs in the check gate against a Python reference on a 120-frame, 30 fps clip. The clip alternates 30 planted frames and 30 swing frames. It also has a 0.03 jitter at frame 10 and a one-frame dip on a second joint.

- With exit bands wider than the enter bands, the foot contacts are `[0, 29]` and `[61, 89]`. The landing frame is too fast to enter.
- Without hysteresis the jitter splits the first plant into `[0, 9]` and `[12, 29]`.
- The dip is dropped at `min_frames` 3 and kept as `[41, 41]` at 1.
- The test also covers:
  - a NaN height;
  - an open-ended contact;
  - a sideways up axis;
  - bad shapes and axes;
  - marker positions;
  - frame-0 and frame-1 speeds;
  - an inverted band;
  - a one-frame clip.

The mutation check caught every non-equivalent mutant. One survivor is equivalent: a held contact whose enter test passes stays held either way.
