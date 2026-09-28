# Controlled limbs with IK/FK and joint limits

`ControlledLimb::solve` operates on a displayed `Pose::Pose` and an explicit
upper/lower/end parent-child chain. The chain may use noncontiguous indices.
Position and end orientation weights are independent. Connected finger/toe
branches follow the end orientation; unrelated branches remain unchanged.

Position weight zero retains FK upper/lower transforms and bend history. It
allows an unreachable inactive positional target. A positive position weight
uses the stable constrained solver, blends upper/lower local rotations with FK,
and projects the final blended chain into authored limits. FK offsets and scales
are retained. Limits take priority if captured FK lies outside them; smoothly
ramping from an invalid FK pose is not guaranteed. Validate authored profiles
against the mocap before runtime activation.

End model orientation is blended from the captured orientation independently of
position, then projected through optional end-joint limits. Upper/lower limits
never change that orientation weight implicitly. End limits may override the
requested orientation. Constraint entries outside the three chain joints reject.
All enabled limits are validated even when their positional weight is zero.

The returned residual and convergence concern the final published end position
against the full positional target. Partial weighting usually has nonzero
residual. Inactive positioning is marked inactive and not converged. Unreachable
active targets return the bounded best pose with residual, not false convergence.
Iterations are bounded to32 and fixed arrays avoid dynamic allocation.

Pose and caller-owned bend memory publish together. Failure leaves both unchanged.
After weighting and limit projection, memory records the actual published bend;
a conflict with the transported bend hemisphere rejects. Reset memory on teleport
or coordinate-space changes. This prevents silent bend flips; it does not provide
velocity-preserving inertialization, whole-body coordination or contact policy.

This API is included in the engine public runtime. Its synthetic fixture is
`test/controlled_limb_main.elisa`. Fighting/sports acceptance additionally needs
character-specific anatomical calibration, support-foot/pelvis and shoulder/guard
coordination, moving supports, interrupted actions, native playback and visual
review. Do not describe this individual limb feature as a complete sports system.
