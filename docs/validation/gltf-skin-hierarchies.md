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
skeleton, then adjusts inverse binds so the bind pose remains unchanged. A
second fixture gives two mesh placements separate two-joint skins. The cooker
combines their rig branches, offsets each placement's joint palette, and
duplicates animation tracks onto the matching rig nodes. Assertions check the
mesh-relative basis, helper rest data, parent ordering, real-joint cluster
indices, a matrix-authored rotation, an animated identity helper, shared
transform cancellation, nested and separately rooted skeletons, the combined
palette remap, and a shear-producing mesh-relative basis rejection. A mixed
fixture adds a static mesh placement; its vertices use a synthetic identity
bind bone while skinned vertices retain their authored palette. The sanitized
native loader also accepts a 65-node rig with two palette bones.

## Validation

- `/opt/homebrew/bin/python3 scripts/gltf_skin_self_test.py` passed.
- `DEVELOPER_DIR=/Library/Developer/CommandLineTools /opt/homebrew/bin/python3 scripts/test_geometry_subsets.py` passed: 99 loader cases, 0 failures, under AddressSanitizer and UndefinedBehaviorSanitizer. The multi-skin and mixed static/skinned packages are among the accepted cases.
- The SDL3/Metal RenderScene smoke compiled and passed the sibling-branch animation submission probe, the separately rooted mesh upload/world-offset assertion, and the multi-skin and mixed-placement armature probes. It later returned status 134 at the overlay-hide readback in `test/render_scene_native_main.elisa`, after those probes completed. This is the same later failure seen with the earlier two-joint fixture, so the full native gate remains incomplete.
- `python3 scripts/check_source_length.py`, `python3 scripts/check_module_hygiene.py`, and `git diff --check` passed.

Multi-skin scenes combine separate rig branches and palettes, bounded by 64
total palette bones and 256 rig nodes. Mixed static and skinned placements
are also supported; static placements receive a synthetic identity bind bone,
which counts toward the palette limit. Morph-weight animation channels remain
unsupported. A
dedicated cooker regression combines a 45-degree mesh rotation with
nonuniform scale, verifies that its mesh-relative basis contains shear, and
requires a clear rejection instead of an approximate TRS transform.
