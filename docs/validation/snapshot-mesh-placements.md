# Static snapshot mesh placements

**Validated:** 2026-09-23 on macOS 27, SDL3 3.4.16, and Wicked Engine 0.72.114.

Snapshot rows for static cooked geometry now preserve placement boundaries in
the native renderer. Each row still has one public instance handle and one
root transform. The root draws placement zero; every later placement gets a
child Wicked object with its own shared mesh. The cooker has already baked
placement transforms into the vertex streams, so child objects use identity
local offsets and follow the root transform.

Each cached mesh is keyed by mesh ID, material ID, and placement index. Its
uploader copies only that placement's vertex and index ranges, rebases indices
to the local vertex range, and clips the merged material subsets to the
placement's index range. The material slot on every clipped subset remains the
one authored by the cooked geometry. Per-row tint values are copied to all
placement objects when a snapshot commits.

Mesh references and child objects participate in the snapshot transaction.
Commit retains all new shared meshes before retiring old rows, allowing a row
to replace itself while reusing the same cached placement meshes. Retire and
destroy remove placement children and release every shared-mesh reference. If
staging succeeds but commit fails, rollback removes every created child and
discards meshes that have no committed references. Instances with snapshot
placement children cannot be cloned through the single-mesh clone path.

## Validation

- The node-hierarchy native test checks four committed placements, their local
  six-index streams, subset ranges, red/red/green/blue slot materials, and the
  shared-mesh count before and after retirement.
- An injected failure after one created row verifies that a failed two-row
  commit restores the instance, object, and shared-mesh counts to their
  starting values.
- `scripts/test_geometry_subsets.py` passed 95 sanitized loader cases.
- `scripts/gltf_hierarchy_self_test.py` passed.
- `scripts/check_source_length.py`, `scripts/check_module_hygiene.py`, and
  `git diff --check` passed.
- The SDL3/Metal native RenderScene smoke passed. The extra packaged-maze
  application stage was not part of this run because its teardown had stalled
  in Metal `waitUntilSignaledValue` after the native gate in the preceding
  run; the packaged-maze stage therefore remains unverified for this slice.

Skinned and morphed snapshot rows continue using the flattened compatibility
upload. Multiple skins per scene and non-identity skinned mesh-node transforms
also remain unsupported. This slice does not claim performance measurements.
