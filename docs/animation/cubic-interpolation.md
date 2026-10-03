# Sampled FK cubic interpolation foundation

`native/animation_cubic_interpolation.h` supplies a bounded, allocation-free
uniform-time shape-preserving Hermite transform segment. Four local transform
keys surround the active interval. Translation/scale and normalized quaternion
components use shared PCHIP tangents. Quaternion neighbours are sign aligned;
output rotation is normalized. Input quaternions may have different magnitudes.
The output is committed only after validation and may alias an input.

Endpoints reproduce normalized key transforms. Interior scalar tangents are
shared across neighbouring segments, avoiding the derivative discontinuities of
linear transform sampling. Scalar no-overshoot does not imply model-joint
acceleration bounds, preserved foot contact, anatomically valid rotation paths
or repaired pose noise. Quaternion normalization also means no scalar rotation
component bound is claimed for the final output.

Invalid pointers, weight, nonfinite transforms, degenerate quaternions and
ambiguous near180degree quaternion neighbours reject without an output write.
Near180degree rejection is an explicit limitation, not an anatomical limit.
The standalone native test covers endpoints, scalar bounds, unit quaternions,
shared numerical derivatives, scaled antipodes, aliases and atomic rejection.

`RenderScene::set_animation_cubic(handle, clip_name, enabled)` configures an
individual clip before playback. Linear translation/scale plus quaternion SLERP
remains the default. Cubic mode uses the same native sampler for displayed poses,
blend sources, corrected-pose rebasing and root-motion extraction. Sampling adds
no allocations. Replacement meshes and restored snapshots reset policy to linear;
interpolation policy is not serialized.

Setup rejects unknown clips, invalid data, active playback/outgoing fades, held
corrections and pending root exits before changing policy. Accepted cubic clips
must have uniform sample times with duration matching the final sample within
0.001 sample intervals. Components are finite and bounded to 1e12, quaternions
nondegenerate with unambiguous adjacent sign branches, and scales nonsingular
without sign changes. The setup validation makes segment evaluation safe; there
is no silent runtime fallback to linear interpolation.

This first integration is **non-looping only**. Attempting to loop a cubic clip
rejects before changing playback state, including continue-same requests. Endpoint
neighbours are clamped, giving zero endpoint tangents. Loop neighbour transport,
root-cycle unwrapping and mismatched seams remain future work. Do not enable this
policy on game assets until moving contact and visual checks pass.

Cooked120Hz poses alone do not preserve authoring Bezier tangents; this primitive
reconstructs smooth interpolation from sampled neighbours. It is not exact
transport of authored tangents. Full fighting/sports acceptance still requires
coordinated support/pelvis/guard corrections, character anatomy, temporal
interruptions and measured native sequences on both fighters and stances.

The native `test/render_scene_animation_cubic_main.elisa` fixture exercises the
current 52-joint boxer package. An off-key held jab differs from linear playback;
repeated readback and differently placed instances agree. Interrupted fades,
horizontal root removal/consumption, model-space overrides, actual Wicked joint
transforms and resampling all pass. Looping a configured action and changing its
policy during playback reject. These checks do not measure visual quality,
foot-contact regressions or acceleration across all clips.
