# Persistent tangent-plane contact anchors

`ContactAnchor::update` receives a selected world-space support point, a unit
surface normal, signed clearance, contact eligibility, elapsed time and settings.
It confirms low/slow contact over time, then retains the acquired anchor across
samples. Separate acquisition/release clearance and speed thresholds provide
hysteresis. Fast travel, loss of eligibility, higher clearance or excessive
separation releases the anchor. New acquisition waits for release to finish.

The output correction lies in the tangent plane. Normal source motion is retained;
vertical penetration correction remains a separate task. This preserves a foot's
vertical mocap motion while allowing toe/heel pivot position to be stabilized.
A caller must intentionally choose a support point and eligibility. Whole-foot
centroids or arbitrary minimum vertices are not automatically valid pivots.

Weight and its velocity follow exact critically damped integration, with
omega=2/acquire_seconds or 2/release_seconds. These parameters control response
rate, not guaranteed fade completion times. Velocity carries through goal changes;
weight bounds clamp out-of-range trajectories. A tiny completed release is snapped
to zero to permit reacquisition. Correction magnitude is bounded independently.
The update stages state privately and typed input errors retain the prior state.
`snapshot` copies state for a larger pose/native-publication transaction.

Reset with `state()` on teleport, coordinate-frame or surface changes. Normals,
positions, clearance and time must be finite; dt is positive and at most0.25s.
Settings require ordered thresholds and positive response/correction bounds.
Confirm time may be zero. The caller controls flight, contact prioritization,
anatomical reach, collision checks and pose publication. The API neither mutates
poses nor implements a complete planted-foot controller.

The fixture covers confirmation, retained slow drift, untouched normal motion,
release/reacquisition separation, high-speed release, correction caps, invalid
input rollback and timestep-subdivision agreement under a fixed damping goal.

The boxing prototype uses explicitly owned state objects passed as direct
references. The first nested mutable-state field version crashed in
ContactAnchor.snapshot under the pinned compiler; do not generalize direct-state
validation to nested-state borrowing. The compiler binding limitation remains
recorded in the game's validation evidence. Reusable anchors also still need
calibrated toe/heel pivots and motion/skin/visual acceptance before gameplay use.
