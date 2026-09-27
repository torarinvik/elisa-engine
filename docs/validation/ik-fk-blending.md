# IK/FK rotation blending

`IkFk::blend_rotations` reconstructs local FK and IK rotations from model poses,
blends each joint by a validated weight, retains FK offsets/scales, and evaluates
the hierarchy again. This avoids the segment shortening caused by blending model
positions. Root/pelvis translations are separate controls; arbitrary IK
translations are not applied by this rotation-only function. Nonuniform scaling
and affine shear retain the existing TRS representation's limitations.

`Rotation::slerp` supplies shortest-arc spherical interpolation in Elisa, using
libc only for scalar sine and inverse cosine. Near-identical rotations use the
stable linear limit. Elisa clip interpolation now uses the same rotation policy
as native cooked playback. Invalid targets/weights retain a valid start, while
an invalid start falls back to identity. The blend layer requires valid unit
quaternion model poses and weights in [0,1].

Pure tests cover zero, quarter, half and full weights, constant angular progress,
non-contiguous chains, unrelated branches, a solved IK target, preserved segment
lengths, opposite quaternion signs, near-equal rotations and malformed inputs.
Native tests with both cooked boxers cover a 15 mm upward foot target, five blend
weights, preserved leg lengths, full-weight target arrival and comparison of all
resulting joint positions with actual Wicked scene transforms (0.2 mm tolerance).
These verify the rotation blend and bridge, not automatic contact detection,
collision freedom, end-effector orientation locking at intermediate weights,
limits or overall animation quality.

Logs are in the boxing project's `build/ik-fk-*`. Final optimized source passes the pure blend and animation-state tests and
native checks on both characters with compiler `04e3085b`, after its shared
rebuild. The fresh game build passes; selected gameplay checks are recorded
separately and are not a substitute for contact/constraint quality validation.
