# Pointer-axis record and replay

`ActionPointerReplay` (src/runtime/action_pointer_replay.elisa) records the pointer events that drive `ActionPointerAxes`, with the tick each arrived on:
- motion;
- wheel;
- focus loss;
- overflow.

Button events never touch the axes, so they are not recorded.
Each delta is quantized to 1/256 pixel before the live axes apply it. A replay therefore rebuilds every frame's look and zoom values bit for bit.

`pointer_begin_frame_recorded` drains the host's pointer queue for a frame and records each event. `pointer_replay_tick` replays one tick into fresh axes.

Entries that are out of order or past the 1024-entry cap still apply live, but they are counted in `dropped` rather than logged.

Byte format: "EPR1", a u32 count, then 12 bytes per entry. Deltas are stored as i16, which caps an event at ±127 px.
`pointer_decode` loads into an empty log. On bad magic, length, kind or padding, or a tick that runs backwards, it leaves the log untouched.

Coverage: test/action_pointer_replay.elisa (codes 1–13; in the gate). It checks 8 ticks at exact f32 values. The ticks mix in button events, a mid-frame focus loss and a backwards re-feed.
Negative control: letting the live axes apply the raw rather than the quantized delta makes the test fail with code 6.
Not yet covered: a recorded native session driving both logs from `Application`, and saving them to disk from a packaged game.
