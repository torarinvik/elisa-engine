# Shared body and limb transaction

`SupportedLimbs::solve` combines `SupportReach` planning and `ControlledLimbs`
against one displayed model-space pose. Supply a shared body root, independent
limb requests, joint constraints and a support mask with calibrated minimum and
maximum distance bands indexed by request slot. At least one support is needed;
supported slots need positive positional IK weight. Bands must fit the current
physical two-segment lengths. All requested limb roots must descend strictly
from the body root. The skeleton's topology identifies the affected subtree.

The body offset is staged privately. All subtree joint positions receive the
same model-space translation, preserving relative FK motion, rotations and
scales; independent limb solving then reaches the fixed targets. A torso/head
branch follows the body translation; branches outside it remain unaffected.
Only after every active positional target converges are pose and all bend
histories published together. A late typed error, unreachable support plan or
nonconverged blended target cannot expose an earlier body or limb correction.

`Result.accepted` is the publication decision. If reach is infeasible, `limbs`
is empty; if final limb convergence fails, limb residuals describe the private
candidate and are diagnostic only. A partial positional weight may prevent full
target convergence and cause rejection. Inactive positional limbs can retain FK
and still receive their independent orientation request. Orientation and skin
clearance acceptance are caller policy, not implicit guarantees of this result.

For a native actor, copy its bend history, read the displayed pose, run this
operation privately, reject `accepted=false`, then override the native pose and
advance the live actor history only after that write succeeds. Reset history on
teleport, coordinate-space changes, rest calibration or skeleton replacement.
The batch binds chains/hierarchy, not body-root or contact-selection semantics;
reset when changing those semantics would invalidate retained bend directions.

This API does not identify planted feet, set contact priorities, derive anatomy,
solve balance or remove sliding automatically. Calibrated bend distance bands
are necessary geometric reach conditions; hinge planes, hip limits, weighted
skin or collisions can still make the desired contact invalid. Check actual
corrected skin and preserve intentional pivots before enabling a controller.
The current boxing game uses this operation only in an explicit native test
probe, not its normal foot policy.

`test/supported_limbs_main.elisa` exercises a shared body/torso translation,
noncontiguous leg chains, fixed ankles and a toe descendant, unaffected outside
branches, history publication, a second-leg error after staging and first-leg
solving with empty/bound history, partial-IK rejection, infeasible support reach
and typed root/band errors. The geometric bands there are synthetic.
