# glTF skin helper hierarchies

**Validated:** 2026-09-23 on macOS 27, SDL3 3.4.16, and Wicked Engine 0.72.114.

Cooked rigs now retain transformed or animated non-joint ancestors on the
skeleton branch below the first skinned mesh node. Those helper transforms are
parent-ordered with the skin joints, while the cluster map continues to name
only actual palette joints. Ancestors shared with the mesh-space root are
factored out, since their transform moves the mesh and skeleton together.
Identity-only helper nodes are omitted unless an animation channel targets
them.

The source node limit remains 256. The runtime distinguishes this hierarchy
bound from the 64-bone skin palette, and animation samples include helper
nodes as well as palette joints. Matrix-authored rig nodes are decomposed to
TRS when their affine linear columns are orthogonal and have nonzero scale;
matrices with shear are rejected rather than approximated.

The generated fixture inserts a translated helper between the mesh-space root
and the two-joint skeleton, then adjusts inverse binds so the bind pose remains
unchanged. Cooker assertions check helper rest data, parent ordering, real-joint
cluster indices, a matrix-authored rotation, an animated identity helper, and
the cancellation of transforms shared with the mesh root. The sanitized native
loader also accepts a 65-node rig with two palette bones.

## Validation

- `/opt/homebrew/bin/python3 scripts/gltf_skin_self_test.py` passed.
- `DEVELOPER_DIR=/Library/Developer/CommandLineTools /opt/homebrew/bin/python3 scripts/test_geometry_subsets.py` passed: 96 loader cases, 0 failures, under AddressSanitizer and UndefinedBehaviorSanitizer.
- The SDL3/Metal RenderScene smoke compiled and ran the transformed-helper animation submission probe successfully. It later returned test status 134 at the overlay-hide pixel check in `test/render_scene_native_main.elisa`; the same status occurred when the cooked skin fixture was temporarily reverted to the previous two-joint hierarchy. The full native gate is therefore not marked green by this run.
- `python3 scripts/check_source_length.py`, `python3 scripts/check_module_hygiene.py`, and `git diff --check` passed.

Multiple skins in one scene remain unsupported. This fixture validates helper
transforms below the selected mesh-space root; independent skeleton branches
and non-identity skinned mesh placement transforms still need end-to-end
coverage before the broader import task can be considered complete.
