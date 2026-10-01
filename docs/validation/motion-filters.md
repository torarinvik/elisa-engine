# Motion-cleanup math (plan M08, first slice)

Two modules give the mocap-cleanup track (docs/plans/mocap-engine-track.md) the
curve and rotation math the boxing game's `tools/clean_leg_motion.py` uses,
in double precision.

- `src/animation/motion_filters.elisa` (`MotionFilters`): one channel of up to
  2048 samples in, one out, with edge or wrap padding.
  - `gaussian`: kernel cut at 3σ and normalized.
  - `median`: running median over an odd window of at most 255 samples.
  - `despike`: the median followed by the Gaussian, as the tool does it.
  - `butterworth`: second-order low-pass, run forward and then backward, so
    there is no phase lag.
  - `quat_continuity`: flips each quaternion sample into the same hemisphere
    as the one before it, so filtering the channels cannot cross the q/−q seam.
- `src/animation/motion_quat.elisa` (`MotionQuat`):
  - `slerp` along the shorter arc.
  - `swing_twist` and a signed `hinge_angle` about an axis.
  - `aim`, the tool's two-axis limb rebuild.
  - f32 `Geometry::Quat` conversion at the edges.

## Evidence

- `test/animation_motion_filters.elisa`: on a 200-sample signal with a spike
  and a dip, the results match numpy runs of the tool's own `smooth` and
  `despike` functions to 1e-9, at seven indices including both ends. That
  covers edge and wrap Gaussian, the 41-sample median, wrap de-spike and a
  6 Hz / 120 Hz Butterworth. The test also checks that invalid parameters are
  refused and that one flipped quaternion sample is restored.
  - Negative control: changing the kernel exponent only where it is applied
    (not where it is normalized) fails with code 2.
- `test/animation_motion_quat.elisa`: slerp end points, midpoint and short
  arc; swing·twist recomposes, and the swing has no component along the axis;
  hinge angles of 0.7, −2.0 and 3.0; `aim` maps +Y and the perpendicular hinge
  onto the world ones; the f32 round trip.
  - Negative control: dropping the factor of 2 in the hinge angle fails with
    code 6.

## Not yet

- The IK half of M08: a pole-driven two-bone solve with knee limits, and
  multi-bone chains over `MotionQuat`.
- Running the whole `clean()` pipeline against a real clip.

## Proof

`proof/motion_filters.elisa` proves that every filter tap stays inside the
signal. `MotionFilterIndex` (edge `clamped`, loop `wrapped`, and `padded`)
returns an index in `[0, n)` for any `i64` index and any `n > 0`. All 28
obligations are proved and replayed.
- Negative control: letting `clamped` return `n` fails the proof.
- To get there, the prover had to stop abandoning a goal when a premise it
  cannot range-check is on the path. `(index % n) < 0` used to block
  `0 < n`; such premises are now set aside. This fix is committed on
  `../elisa-engine-proof`.

## Quaternion normalisation proof (2026-10-01)

`MotionQuat::normalize` and `vnormalize` used `return ... if length <= EPSILON`. NaN fails every
comparison, so a NaN quaternion slipped past that guard and was divided by a NaN length. Both now
use `not (length > EPSILON)`, and the division goes through `MotionQuatGuard::unit` /
`MotionQuatGuard::scale` (src/animation/motion_quat_guard.elisa). `scale` declares
`requires length > EPSILON`. `slerp` likewise sends a NaN cosine to the normalized-lerp branch
(`not (c <= NEAR_ONE)`), which returns the identity.

Proof: `proof/motion_quat_guard.elisa` proves 8/8 obligations with 8 certificates replayed and 0
gaps. It uses the prover's new float mode (elisa-engine-proof, AUDIT "Floating-point functions are
checked by syntactic containment"). Functions with float parameters are checked by syntactic fact
containment only, assuming no IEEE arithmetic, so NaN, rounding and signed zeros cannot make a
claim true that is false at run time. The prover on elisa-proof main still refuses the file
(`unsupported`). Mutant: changing `unit`'s guard to `length <= EPSILON` fails the proof with
`call-requires-unproven`.

Tests: `test/animation_motion_quat.elisa` checks 13–18. NaN and zero quaternions normalize to the
identity, a NaN vector to zero, `slerp` towards a NaN quaternion gives the identity, and `unit` /
`divides` return the fallback for NaN, zero and negative lengths. With the old `<=` guard
restored, the test fails at check 13.
