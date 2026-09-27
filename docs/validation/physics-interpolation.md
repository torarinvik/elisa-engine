# Physics interpolation validation

`src/physics/interpolation.elisa` separates fixed-step publication from render
sampling. Simulation commits monotonically increasing ticks and publishes the
previous/current transforms; rendering samples a bounded alpha without changing
the tick. Teleports bypass blending for one sample, preventing a long smear
across an origin reset or respawn.

`test/physics_interpolation.elisa` covers fractional samples, duplicate-tick
rejection, teleports, and pose validity. `src/world/hierarchy.elisa` stores the
latest authoritative world pose alongside the previous committed pose, and
`src/runtime/world_rendering.elisa` exposes `extract_hierarchy`, which samples
that history for presentation without mutating `World`. `test/hierarchy.elisa`
and `test/world_rendering.elisa` verify parent-aware publication and a midpoint
render sample while the authoritative transform remains at the latest tick.
`native/physics_interpolation_probe.h` mirrors the bounded publication contract
at the Wicked boundary and the native gate verifies fractional interpolation,
duplicate rejection, and teleport bypass. `test/clock.elisa` drives separate
30 Hz and 120 Hz presentation loops over the same elapsed microseconds,
publishes each due fixed pose, and verifies equal committed ticks, accumulator
remainders, and sampled render poses. The portable and native interpolation
probes also sample identical committed histories at both cadences. The adjacent
real-Jolt body gate verifies kinematic target publication plus dynamic sleep and
wake stability. `test/world_hierarchy_render_native_main.elisa` now validates
the complete public session path from a kinematic hierarchy target through a
Jolt fixed tick and interpolated snapshot into a live Wicked render and pick.
The same native session client verifies pause freezes a bound Jolt body,
teleports bypass interpolation after the Jolt commit, and a 100-tick elapsed
hitch is limited to four physics steps. `test/world_physics_session_cadence_native_main.elisa`
also drives a Jolt body through public `RuntimeServices` at 30 Hz and 120 Hz,
checks each frame's fixed-tick count, and verifies equal final poses. The
SDL3/Metal renderer captures every shared-time frame (30 images per schedule),
and the native gate confirms that all 640x480 RGBA pixels match. Together these
checks close P04's fixed-step acceptance criteria.

`Runtime::StepClock` can also be paused independently of the application host. While paused,
advancing ignores wall time and leaves the tick and interpolation remainder untouched. Resume
continues from that remainder without simulating the time spent in a menu. The public
`RuntimeServices` session reports this state in `SimulationFrame` and exposes a checked pause
setter; `test/clock.elisa` covers freeze and resume behavior.
