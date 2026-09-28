# Atomic multi-limb IK/FK

`ControlledLimbs::solve` corrects one displayed `Pose::Pose` with one to four
independent arm/leg chains. It uses `ControlledLimb` for independent position
and end-orientation weights, authored joint limits and stable bend transport.
All limb results and histories publish together with the candidate pose. A
failure on any limb leaves the input pose and the entire actor state unchanged.
Unreachable targets are successful bounded results with per-limb residuals;
they are not reported as converged.

Construct requests with `ControlledLimbs::requests()` and persistent actor
history with `ControlledLimbs::state()`. Each active request supplies upper,
lower and end indices, model-space target/orientation, separate weights, bend
hint, tolerance and iteration budget. Use fixed limb slots across frames and
zero positional weight to release a hand during a punch or a foot during flight.
End orientation remains independently controllable. Each limb permits at most
32 iterations; all arrays have fixed capacity and the solver allocates no heap
storage.

Pass one `PoseJointLimits::Constraints` with the displayed pose's joint count.
Enabled entries must belong to one of the requested three-joint chains. The
batch partitions those entries for each limb instead of silently ignoring an
unowned limit. Roots of controlled branches must be unrelated in the hierarchy:
duplicate, shared-root and nested branches reject. This guarantees that a later
solve cannot invalidate an earlier branch's target or its returned diagnostics.
Finger/toe descendants still follow their end joint normally.

State binds the slot count, chain indices and complete parent hierarchy on the
first successful solve. Changing those without resetting state rejects. Rest
calibration, skeleton replacement with the same hierarchy, teleport and solver
coordinate changes require an explicit reset; the API does not infer actor
identity or compare all rest transforms. Targets and weights may change without
resetting bend history.

The transaction covers Elisa pose/state values. For native publication, solve
using a private actor-state copy from `ControlledLimbs::snapshot` and commit that copy only after the native pose
override succeeds. Do not advance history on a failed native write. Root-motion
extraction remains upstream of displayed-pose capture.

This is a publication building block for a coordinated fighting/sports pipeline,
not a whole-body optimizer. Pelvis/support anchors, shoulder coordination,
contact priorities, anatomical calibration, skin clearance and velocity-preserving
interruption handling remain required. Separate constrained limbs cannot prove
a natural guard or planted feet.

`test/controlled_limbs_main.elisa` exercises four noncontiguous chains under one
parent, joint constraints and following descendants, last-limb rollback both
before and after state binding, duplicate/nested branch rejection, unowned
constraints, incompatible state/hierarchy, unreachable and independently weighted
targets, and41 near-straight updates with singular bend hints. Synthetic
fixtures do not establish native character or all-clip visual acceptance.
