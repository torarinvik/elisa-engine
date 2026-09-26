# Physics constraints and grabs

`PhysicsRuntime` and `RuntimeServices` expose generation-checked handles for
fixed, point, distance, hinge, slider, cone, six-degree-of-freedom, and
swing-twist constraints. A descriptor names two managed body handles, a
world-space anchor, and a frame rotation. Distance and slider limits use Elisa
world units; six-degree-of-freedom translation limits use three local axes;
hinge, cone, swing, and twist limits use radians. The frame's local up axis
drives hinges and its local right axis drives sliders.

Hinge and slider motors accept a target velocity and a maximum torque or force.
Setting velocity to zero disables the motor. Distance-based breaking is
disabled by a zero `break_distance`; otherwise the joint is disabled when the
two body centers move farther apart than their initial separation plus that
threshold. `constraint_is_broken` reports whether Jolt has disabled the joint.

Destroying a body first removes every managed constraint that references its
slot and generation. Those constraint handles then return `InvalidHandle`.
Explicit constraint destruction removes its Wicked entity, which releases the
Jolt constraint before either body can be removed.

The grab API accepts world-space rays and fixed or point grab modes. A miss
returns `PhysicsError.NoHit`. The native layer cancels all active grabs before
any managed body is destroyed because Wicked's current `PickDragOperation`
keeps an internal body pointer. This makes body removal safe, and invalidates
the affected grab handles. A grab's break distance uses the same initial
separation rule as a regular constraint.

`BodyDesc.sensor` and the existing contact queue provide sensor/trigger
behavior for interactables. Their contact events remain bounded and delivered
through the established physics contact API.

## Validation

The hidden SDL3/Metal `physics-constraints-smoke` creates point, cone,
six-degree-of-freedom, and swing-twist joints and checks their live state. It
also drives a bounded slider motor, verifies a separate slider breaks at its
configured separation, destroys a point-joint endpoint and checks handle
invalidation, and grabs/moves/releases a body. A ray miss maps to `NoHit`, and
destroying a grabbed body cancels its handle. The test runs through the public
Elisa API and native Jolt scene:

```sh
ELISA_NATIVE_SMOKE_ONLY=physics-constraints-smoke \
  python3 scripts/application_native_smoke.py
```

`examples/physics_interactables` adds a public-API sample with a motorized
hinge door, vertical slider lift, and point-jointed pendulum. Its hidden
acceptance client verifies door/lift travel, checks that all three joints stay
intact, and separately verifies vertical slider motor travel through the
`RuntimeServices` session route. The direct `PhysicsRuntime` smoke also checks
a rotated vertical slider and its limits. Both the sample and constraint
smokes passed on 2026-09-26.
