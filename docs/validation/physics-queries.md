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
sphere/capsule overlaps, bounded all-hit storage, destroyed-participant
rejection, stale-token rejection, and target unload back to the object baseline.
Run:

```text
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
ELISA_ALLOW_STALE_STAGE1=1 \
~/.local/bin/elisascript scripts/wicked_probe.elisascript
```

Shape casts, trigger/contact event queues, and callback thread handoff remain
follow-up P03 work; this adapter establishes the query ownership and filtering
boundary those services will use.
