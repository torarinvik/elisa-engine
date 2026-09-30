# Two-bone leg IK with pole and knee-turn limit (M08)

`LegChain` (src/animation/leg_chain.elisa) ports the leg rebuild from the
boxing game's tools/clean_leg_motion.py.

- `pole_of` finds the knee direction about the hip-ankle line.
- `chain` is the two-bone solve from a fixed hip towards the cleaned ankle,
  bending towards the pole. A target out of reach stops 0.1 mm short of
  straight and reports the miss. The thigh and shin rotations are rebuilt
  with `MotionQuat::aim` from the source knee hinge.
- `ankle_wring` measures the foot's rotation against the shin, away from
  the guard pose, excluding flex.
- `soft_limit` eases wringing above the free angle into a ceiling.
- `knee_turn` searches 1-degree steps, up to 50 either way. It returns the
  smallest turn of the knee about the hip-ankle line that brings wringing
  under the soft limit, and falls back to the least-wringing turn.

## Proofs

proof/leg_chain_index.elisa, 24/24 with every obligation replayed:

- every search step turns by 0 to 50 degrees;
- live steps turn by exactly `step` degrees;
- steps outside the search turn by nothing;
- the search tries 100 candidates.

Two prover holes turned up while writing these: order goals against a
negative constant, and constant-offset goals on guarded returns. Both are
filed as a separate prover task. The index works around them by returning
the turn's size, with the sign applied in the float code.

## Tests

- test/animation_leg_chain.elisa checks:
  - bone lengths are kept, and the knee bends towards the pole;
  - the thigh and shin carry +Y along their bones;
  - an out-of-reach target reports its miss exactly;
  - the soft limit is the identity below the free angle, capped above it
    and continuous at it;
  - a calm foot needs no turn;
  - a foot twisted 20 degrees is brought under the limit by the smallest
    working turn.

  A mutated wring expectation fails with exit 12.
- test/animation_leg_chain_reference.elisa compares the knee, thigh, shin,
  miss, wring, limit and chosen turn (14 degrees) with values from
  tools/leg_chain_reference.py. That script runs the Python logic verbatim
  under Blender's mathutils. Everything matches to 1e-4, which is float32
  reference precision.

Still open for M08: multi-bone chains, the grounded heel/ball pivot and
pelvis smoothing checks, and quaternion-normalisation proofs.
