# Native physics queries

`native/physics_query_bridge.h` is the bounded collision-query boundary for the
Wicked backend. It keeps Wicked entity and intersection records private and
returns copied `PhysicsQueryHit` values to the host.

## Contract

- A query begins with a generation-checked `PhysicsQueryToken` owned by one
  `PhysicsQueryBridge` instance.
- Raycasts validate finite origin/direction values, reject zero directions and
  non-positive distances, and support a caller-provided layer and filter mask.
- `raycast_all` copies at most `MAX_HITS` (16) results into fixed storage. The
  native scene may collect more internally, but the Elisa-facing result is
  bounded.
- Sphere and capsule casts use a fixed bounded sweep with refinement and return
  the first copied hit distance. They share the same finite-input, layer, and
  filter validation as overlaps.
- `PhysicsContactQueue` accepts contacts from worker threads into 64 fixed
  slots, reports overflow, normalizes entity pairs, and sorts delivery by pair
  and contact kind on the owner thread. Reentrant drains and non-owner drains
  are rejected, and each accepted event is delivered once.
- The physics service installs that queue listener for every initialized Jolt
  world. `PhysicsRuntime::poll_contacts` copies the bounded event batch into a
  public Elisa `ContactBuffer` with no native handles or retained pointers;
  polling also returns the explicit overflow count. The C ABI uses scalar
  `contact_count`, `contact_at`, and `clear_contacts` accessors so the public
  boundary does not depend on aggregate C struct calling conventions. Unpolled
  batches accumulate up to the fixed 64-event boundary and are cleared only by
  a successful poll.
- Sphere and capsule overlaps return the nearest Wicked result with copied
  position, normal, and penetration depth. Their all-hit variants copy at most
  `MAX_HITS` unique entities, even when Wicked reports several intersected
  mesh subsets for one object. Invalid radii and non-finite inputs are
  rejected.
- Destroying a scene participant and rebuilding the scene removes it from the
  next query. Invalidating the token rejects every later query, including
  foreign-owner tokens.

## Evidence

The native Wicked gate creates a layered cube, updates its scene BVH, verifies a
nearest ray hit, sphere/capsule casts, layer misses, nearest and all-hit
sphere/capsule overlaps, bounded all-hit storage, contact queue overflow and
worker handoff, then creates overlapping static and dynamic Jolt bodies. The
real Jolt listener path verifies worker-thread `Added` delivery, sensor-state
classification for ordinary bodies, cached `Removed` delivery after a
participant is destroyed, deterministic owner-thread draining, stale-token
rejection, and target unload back to the object baseline. Run:

```text
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
ELISA_ALLOW_STALE_STAGE1=1 \
~/.local/bin/elisascript scripts/wicked_probe.elisascript
```

`wi::physics::SetContactEventListener` keeps the callback pointer atomic and
clears it before the physics scene destroys its fixed removal cache. Jolt body
and sub-shape identity stays inside Wicked; the Elisa-facing listener receives
only copied values, and queue overflow remains explicit rather than allocating
from worker callbacks. A caller must unregister its listener before destroying
the scene or listener object.

The Elisa application smoke creates static and dynamic bodies, advances the
fixed-step world, and polls `PhysicsRuntime::ContactBuffer`. It requires a real
non-trigger `ContactKind.Added` event and zero dropped events, proving the
public binding reaches the same Jolt callback queue used by the native gate.
