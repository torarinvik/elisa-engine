# Grounded foot pivot

`src/animation/foot_pivot.elisa` ports the foot-pivot step of the boxing
game's `tools/clean_leg_motion.py`. A corrected foot turns about its lower
contact (heel or ball, softly blended within about a centimetre), and the
ankle is lifted or lowered by the ground weight so the contact keeps its
height instead of sinking or floating.

- `edge_fade(frame, count, edge)`: smoothstep ramp that is 0 at both clip
  ends and 1 from `edge` frames in.
- `ground_weight(height, low, high)`: 1 at or below `low`, 0 at or above
  `high` (the tool uses 0.015 m and 0.04 m).
- `heel_local`, `heel_at`: the heel point under the ankle, carried in foot space.
- `pivot_point`, `pivoted_ankle`: the contact and the new ankle position.

## Proofs

`proof/foot_pivot_index.elisa` covers `FootPivotIndex::edge_steps`, which
counts frames to the nearer clip end capped at `edge`. The proof shows the
result stays within `0..edge`, is 0 at the first and last frames, and is
nonzero only strictly inside the clip. That gives 39/39 goals and certificate
replay with 0 gaps.

## Tests

- `test/animation_foot_pivot.elisa` checks properties:
  - the fade ends and midpoint;
  - ground clamping;
  - the pivot sitting on the lower contact;
  - a fully grounded turn keeping the lowest contact at its height;
  - identity leaving the ankle in place.
- `test/animation_foot_pivot_reference.elisa` checks against
  `tools/foot_pivot_reference.py`, which runs the tool's lines in Blender
  mathutils (float32), to 1e-4.
- Mutation check: dropping the grounding lift fails both tests (exits 6 and 5).
