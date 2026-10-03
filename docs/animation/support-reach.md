# Shared support reach

`SupportReach::solve` selects a scalar translation along a unit model-space axis
that puts up to four origins within their distance bands from fixed targets.
For legs, origins are hip joint positions, targets are desired ankles, and the
bands come from segment lengths and calibrated knee bend limits. It supplies
shared-body reach planning before independent limb IK; it does not select which
feet are planted or solve full-body balance.

Inputs include bounded minimum/maximum translation, a preferred translation and
a nonnegative distance tolerance. The solver finds a feasible geometric offset
nearest the bounded preference, checking preference, caps and inner/outer sphere
boundaries. Equally distant candidates choose the lower scalar offset. Inner
spheres can split the allowed translations into disconnected intervals. Four
supports require at most 19 candidates and 76 distance checks; storage is fixed.
If nothing fits, `feasible` is false, `offset` is the bounded preference and
`maximum_distance_error` is the violation at that offset. It is not a minimax
fallback. Do not apply an infeasible offset as a successful support correction.

All positions and bounds must be finite. The axis must be within 0.0001 of unit
length and is normalized internally. Invalid counts, inverted/nonpositive bands,
inverted bounds and nonfinite intermediate sphere calculations raise
`ReachError.InvalidInput`. Zero minimum distance is allowed. The caller's
coordinate units also determine the distance tolerance.

The API neither mutates poses nor retains temporal history. Stage the returned
body adjustment in a private displayed pose, then run `ControlledLimbs` on the
chosen targets, validate residuals and skin clearance, and publish pose/history
only after native write succeeds. Release, reach priority, support selection,
limits for other joints and motion continuity belong to the caller/controller.

The geometric distance bands are necessary reach conditions, not sufficient
proof that the constrained IK pose exists. A hinge's allowed plane, hip limits,
contact orientation, body collisions or competing supports can still prevent it.
Targets derived from weighted skin patches need actual corrected skin readback;
translating an ankle by patch displacement assumes rigid local foot geometry.
The boxing game does not yet enable this controller in gameplay.
