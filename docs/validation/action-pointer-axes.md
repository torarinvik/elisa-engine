# Pointer motion and wheel axes

Validated on 2026-09-30 with the pinned Stage1 compiler. This is I01
progress on "mouse motion/scroll".

`src/runtime/action_pointer_axes.elisa` turns relative `Application`
pointer events into per-frame action values. A binding maps a source
(MotionX, MotionY, WheelX or WheelY) to an action with a scale, so
sensitivity and inverted look are settings.

- Values accumulate within a frame and reset at `pointer_axis_begin_frame`.
  Motion is never held like a button.
- Focus loss and queue overflow drop the partial frame.
  `pointer_axis_set_enabled(false)` ignores motion while a menu owns the
  pointer.
- One frame's value is clamped to ±10,000 per binding.
- There are at most 8 bindings. The map refuses action ≤ 0, a zero, NaN or
  |scale| > 1000 scale, and a duplicate action+source pair.

## Test

`test/action_pointer_axes.elisa` is in the gate list. It checks:

- two motion events accumulate (look X 5.0, look Y −0.5), and the wheel
  feeds only zoom;
- the next frame starts at zero;
- a negative scale inverts Y;
- focus loss clears the frame, and a disabled map ignores motion;
- a flung mouse is clamped;
- capacity is enforced.

Negative control: storing the unclamped total makes the test fail with
code 11.

## Gaps

- Games still drain `Application::next_pointer_event` themselves.
  ActionInputRuntime does not route pointer events into these axes.
- Pointer axes are not recorded by ActionInputReplay.
- There is no raw or relative-mode mouse capture, and no physical-mouse run.
