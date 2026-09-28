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

This is not yet connected to native playback or exposed as an Elisa policy.
Playback integration must use one sampler for blends, corrected-pose readback,
skinning and root-motion extraction. An opt-in policy must be configured before
playback or transition safely from the displayed pose; changing it mid-action
must not cause pose or root displacement jumps. Loop neighbour transport and
root-cycle unwrapping require explicit treatment, including duplicated endpoints,
short clips and mismatched loop seams. Preserve the existing linear default until
native tests and actual moving character contact/visual checks pass.

Cooked120Hz poses alone do not preserve authoring Bezier tangents; this primitive
reconstructs smooth interpolation from sampled neighbours. It is not exact
transport of authored tangents. Full fighting/sports acceptance still requires
coordinated support/pelvis/guard corrections, character anatomy, temporal
interruptions and measured native sequences on both fighters and stances.
