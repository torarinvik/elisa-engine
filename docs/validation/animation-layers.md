# Animation layers and root motion

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the Stage1 compiler.
This is C04 progress.

## Design

`src/animation/layers.elisa` (`AnimLayers`) works on one integer channel per
joint (up to 16), such as a rotation in millidegrees.

- `override_joint` and `override_pose` blend a layer over the base by a
  layer weight times a per-joint mask, both clamped to 0..1000.
- `additive_joint` adds the layer's offset from its reference pose.
- `root_at` and `root_delta` extract root motion from a looping clip by
  counting whole cycles, so accumulated motion is exact for any playback
  length.

## Checks

`test/animation_layers.elisa` exits 0. It covers weights and clamping,
additive lean at full and half weight and under a zero mask, an upper-body
mask that changes exactly joints 8–15, root positions across cycles, a delta
across a loop boundary, and one hour of 60 Hz deltas summing to exactly
3600 cycles. Negative control: dropping the whole-cycle term from `root_at`
makes the test exit 10.

## Gaps

Channels are scalar, not quaternions; no sampler, skeleton mask asset or
physics-driven root motion yet, so C04 stays open.
