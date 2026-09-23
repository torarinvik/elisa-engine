# glTF skin helper hierarchies

**Validated:** 2026-09-23 on macOS 27, SDL3 3.4.16, and Wicked Engine 0.72.114.

Cooked rigs now retain transformed or animated non-joint ancestors on the
skeleton branch, including when the mesh and skeleton occupy sibling branches
under a transformed scene parent or the skeleton has a separate root. Those
helper transforms are parent-ordered with the skin joints, while the cluster
map continues to name only actual palette joints. Ancestors shared with the
mesh-space root are factored out, since their transform moves the mesh and
skeleton together. A synthetic mesh-relative basis node preserves the branch
offset without reapplying the mesh transform. Identity-only helper nodes are
omitted unless an animation channel targets them.

The source node limit remains 256. The runtime distinguishes this hierarchy
bound from the 64-bone skin palette, and animation samples include helper
nodes as well as palette joints. Matrix-authored rig nodes are decomposed to
TRS when their affine linear columns are orthogonal and have nonzero scale;
matrices with shear are rejected rather than approximated.

The generated fixture places the mesh and skeleton on sibling branches under
a translated common parent, inserts a translated helper above the two-joint
skeleton, then adjusts inverse binds so the bind pose remains unchanged.
Cooker assertions check the mesh-relative basis, helper rest data, parent
ordering, real-joint cluster indices, a matrix-authored rotation, an animated
identity helper, cancellation of shared transforms, a nested helper below the
mesh node, and a separately rooted skeleton. The sanitized native loader also
accepts a 65-node rig with two palette bones.

## Validation

- `/opt/homebrew/bin/python3 scripts/gltf_skin_self_test.py` passed.
- `DEVELOPER_DIR=/Library/Developer/CommandLineTools /opt/homebrew/bin/python3 scripts/test_geometry_subsets.py` passed: 97 loader cases, 0 failures, under AddressSanitizer and UndefinedBehaviorSanitizer. The new separately rooted package is among the accepted cases.
- The SDL3/Metal RenderScene smoke compiled and passed the sibling-branch animation submission probe and the separately rooted mesh upload/world-offset assertion. It later returned test status 134 at the overlay-hide pixel check in `test/render_scene_native_main.elisa`; the same status occurred when the cooked skin fixture was temporarily reverted to the previous two-joint hierarchy. The full native gate is therefore not marked green by this run.
- `python3 scripts/check_source_length.py`, `python3 scripts/check_module_hygiene.py`, and `git diff --check` passed.

Multiple skins in one scene remain unsupported. The separately rooted fixture
now passes cooking, sanitized package loading, and native mesh upload. A
mesh-relative basis that would contain shear is rejected by the cooker, but
that specific transform combination still needs a dedicated regression case.
