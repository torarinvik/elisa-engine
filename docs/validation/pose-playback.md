# Clip and live-override playback into a render pose (M02)

`PosePlayback` (src/animation/pose_playback.elisa) drives the pose shown on a
skinned instance from either source:

- a `MocapClip` scrubbed to any microsecond (`show_clip`);
- the app's live override (`show_override`).

`switch_source` holds the pose on screen and starts a `PoseFade`, so every
switch blends in rather than popping. Bones that the new source does not
cover keep their pose. Poses are one-frame `MocapClip` buffers, so every
write goes through the proved clip slot index.
`RenderScene::fill_pose_from_playback`
(src/runtime/render_scene_pose_playback.elisa) copies the shown locals into
an `AnimationPose` with `set_bone_transform`. The native bridge applies
those locals as-is.

## Proofs

proof/pose_playback_index.elisa, 43/43 with every obligation replayed:

- the bones written are never negative and never above the 256-bone render
  limit;
- they never exceed what either the source or the shown pose holds;
- an empty source writes nothing;
- a held-bone hit is always in range.

## Tests

- test/animation_pose_playback.elisa:
  - scrubbing between frames;
  - a switch to the override that moves nothing at the switch instant;
  - the halfway blend and the full override;
  - an un-overridden bone that keeps its pose;
  - a fade back to the clip.

  A mutated halfway value fails with exit 7.
- test/animation_pose_playback_render.elisa: fills a 70-bone
  `AnimationPose` from the shown buffer without a native device.

Still open for M02: the render-side p95 frame time on a live instance.
