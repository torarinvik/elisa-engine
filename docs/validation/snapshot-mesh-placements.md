# Snapshot mesh placements

**Validated:** 2026-09-23 on macOS 27, SDL3 3.4.16, and Wicked Engine 0.72.114.

Snapshot rows preserve cooked placement boundaries in the native renderer.
Each row still has one public instance handle and one root transform. The root
draws placement zero; every later placement gets a child Wicked object. The
cooker has already baked static placement transforms into vertex streams, so
static child objects use identity local offsets and follow the root transform.

Each cached mesh is keyed by mesh ID, material ID, and placement index. Its
uploader copies only that placement's vertex and index ranges, rebases indices
to the local vertex range, and clips the merged material subsets to the
placement's index range. The material slot on every clipped subset remains the
one authored by the cooked geometry. Per-row tint values are copied to all
placement objects when a snapshot commits.

Static mesh references and all child objects participate in the snapshot transaction.
Commit retains all new shared meshes before retiring old rows, allowing a row
to replace itself while reusing the same cached placement meshes. Retire and
destroy remove placement children and release every shared-mesh reference. If
staging succeeds but commit fails, rollback removes every created child and
discards meshes that have no committed references. Instances with snapshot
placement children cannot be cloned through the single-mesh clone path.

Animated geometry uses independent per-instance meshes so separate snapshot
instances cannot share a mutable armature or morph weights. The root placement
creates the armature and joint entities; later placements slice their own
vertex, index, subset, skin-influence, and morph-target ranges and point at the
root armature. The animation submission bridge includes snapshot children when
it submits a pose, applying shared bones and morph weights across the full
placement group.

## Validation

- The node-hierarchy native test checks four committed placements, their local
  six-index streams, subset ranges, red/red/green/blue slot materials, and the
  shared-mesh count before and after retirement.
- An injected failure after one created row verifies that a failed two-row
  commit restores the instance, object, and shared-mesh counts to their
  starting values.
- The animated snapshot case injects rollback after creating a skinned root
  and child, then retries. It verifies both placements, submits a bone and
  morph pose through the animation ABI, checks the child receives the same
  armature and morph weight, and confirms clearing removes the full group.
- `scripts/test_geometry_subsets.py` passed 95 sanitized loader cases.
- `scripts/gltf_hierarchy_self_test.py` passed.
- `scripts/check_source_length.py`, `scripts/check_module_hygiene.py`, and
  `git diff --check` passed.
- The static-placement revision's SDL3/Metal native RenderScene and packaged-
  maze stages passed on the isolated engine. The maze ran in a sandbox denying
  checkout access; missing, escaping, corrupted, and dependency-invalid bundles
  failed asset registration, and restoring the valid bundle returned exit 0.
- With animated per-placement upload in `3f83c4b`, the SDL3/Metal native
  RenderScene smoke passed, including the animated snapshot case. An earlier
  app teardown stalled in Metal `waitUntilSignaledValue` after the native gate,
  so the app and packaged-maze stages were rerun after merging; both passed.
  The packaged maze ran in the checkout-denying sandbox, rejected missing,
  escaping, corrupted and dependency-invalid bundles, then returned exit 0 when
  the valid bundle was restored.

Multiple skins per scene and transformed non-joint ancestors of joint nodes
remain unsupported. Skinned mesh-node transforms are retained as metadata and
ignored by cooked streams as required by glTF. This slice does not claim
performance measurements.
