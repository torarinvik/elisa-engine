# Render-scene animation schedule

`RenderSceneAnimationSchedule` advances Elisa-selected Wicked clip instances as
one bounded frame update. Each active instance receives a unique positive
`AnimationKey`; the schedule and its native handles stay private to
`RenderScene`. Playback registration happens only after the backend accepts the
clip, duplicate keys are rejected, and the fixed-size table allocates no memory
while advancing. Pause/resume gates the whole set. `destroy` removes the native
instance and its schedule entry together; callers that destroy an instance
through another API must `forget` its key first.

The scheduler rejects negative, non-finite, and greater-than-one-second frame
deltas before touching any instance. An update reports how many tracked
instances it advanced. A backend error stops that update at the failing entry;
instances earlier in the table may already have advanced.

The SDL3/Metal render-scene smoke verifies two independent animated/morphed
instances advance from one scheduler call, paused updates leave both poses
unchanged, duplicate keys and invalid deltas are rejected, removing one player
does not stop the other, and destroy clears each schedule entry. Run it with:

```sh
ELISA_RENDER_SCENE_RENDER_ONLY=1 \
ELISA_COMPILER_BIN=../Elisa-compiler/bin/elisac-stage1 \
WICKED_ROOT=../amazing-labyrinth-wickedengine \
WICKED_BUILD=../amazing-labyrinth-wickedengine/build-elisa-sdl3 \
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
python3 scripts/render_scene_native_smoke.py
```

`RenderScene::animation_pose_from_clip` samples an `AnimationAssets` skeleton
and clip in Elisa and copies the resulting model transforms into the same fixed
`AnimationPose` payload. Morph weights remain independently editable before the
checked double-buffered submission. The native smoke samples an endpoint pose,
submits it through Wicked, and checks that an out-of-range tick maps to
`RenderSceneError.InvalidValue`.

`RenderSceneAnimationSchedule` advances Wicked clip players already loaded by
a rendered instance. `SampledPoseSchedule` separately feeds freshly sampled
Elisa poses every frame. Animated clones share their decoded cooked package;
their Wicked mesh components, armatures and bones stay separate so each can
deform and submit a pose independently. Clone ownership is covered by group
236 (`test/render_scene_animation_clone_native.elisa`), and sampled poses by
group 237 (`test/render_scene_pose_schedule_native.elisa`).

## Sampled Elisa poses each frame (2026-10-02)

`RenderScene::SampledPoseSchedule` (`src/runtime/render_scene_pose_schedule.elisa`)
feeds poses sampled in Elisa to up to 16 animated instances (the native
bridge's limit). The instances share one Elisa skeleton and clip, which suits
clones from `create_animated_mesh_instance`, and each keeps its own tick.
`sampled_pose_advance(schedule, skeleton, clip, delta)` advances every tick
with a looping wrap at the clip length, samples the clip with
`animation_pose_from_clip`, applies the schedule's morph weights and submits.
An instance whose last pose no frame has read yet is deferred rather than
overwritten; a pose a frame has read is completed first (waiting for that
frame if it is still running). `sampled_pose_settle` retires the remaining
poses before cleanup. The tick wrap and slot choice live in
`src/runtime/pose_schedule_index.elisa` and are proved in
`proof/pose_schedule_index.elisa` (28 obligations, zero findings).
`AnimationAssets::clip_duration_ticks` exposes the clip length.

Evidence: render smoke group 237 tracks a cooked source and its clone at ticks
10 and 5, rejects a negative start tick and delta, submits both, defers both
when no frame has run, then after a pump finds bones at x 2.0 and 1.0. Five
ticks later the source wraps to tick 5 and the clone reaches 10, and the
bones swap to 1.0 and 2.0. Settling leaves nothing pending and cleanup
returns the instance count. Negative controls: submitting without deferral
fails case 11, dropping the tick write-back fails case 17, and removing the
spelled-out wrap bound leaves 4 obligations unproven.
