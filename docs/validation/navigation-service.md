# Navigation service validation

The native navigation boundary is `native/navmesh_service.h`. It owns the
Recast intermediate buffers and the Detour mesh/query objects through one
`NavMeshArtifact`; Elisa remains responsible for agent intent and movement.

## Evidence

Run from the engine root:

```text
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
ELISA_ALLOW_STALE_STAGE1=1 \
ELISA_COMPILER_BIN="$PWD/../Elisa-compiler/scripts/elisac_stage1.sh" \
~/.local/bin/elisascript scripts/wicked_probe.elisascript
```

The native gate passed on 2026-09-19. Its Recast/Detour output included:

```text
recast: polys=16 navdata=2476 path_polys=5 path_points=2 source=17
```

The fixture builds a deterministic wall with a gap, records source generation,
grid dimensions, polygon count, and serialized tile size, then queries a route
around the wall. It also exercises nearest-point and walkability-ray queries.
Invalid triangle indices, null positions, zero extents, and a filtered-out area
are rejected with explicit `InvalidInput` or `NoPath` results. The adapter has
fixed query capacities and reports `Partial` when a result exceeds them.

`NavMeshArtifact` releases the Detour query and mesh and all Recast allocations
on every path, including failed allocation and serialization paths. The
generation-checked `NavMeshTileStore` publishes bounded artifacts, rejects
stale handles after unload, and exposes the same bounded query result without
letting a pending caller use a replaced tile. The copied serialized tile is
available to a future versioned cook cache; multi-tile async streaming is still
open.

## Scope

This is an integrated native adapter used by the real Wicked validation host,
not yet a complete gameplay navigation service. The existing Elisa maze still
uses its bounded BFS policy. N01 remains open for multi-room/stair bake assets,
area/link metadata, and debug overlays; N02 remains open for gameplay agent
corridor following and richer area/link queries. N03 and N04 are not claimed by
this probe.
