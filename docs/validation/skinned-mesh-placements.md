# Skinned and morphed mesh placements

**Validated:** 2026-09-22 on macOS 27, SDL3 3.4.16, and Wicked Engine 0.72.114.

`RenderScene::create_mesh` now builds a Wicked mesh for every cooked placement.
Each entity receives only its recorded vertex, index, subset, tangent, skin
influence, and morph-target ranges. The root mesh owns the armature and joint
entities; child meshes point at that same armature. Animation submission updates
the shared bones once and applies each morph weight to every placement mesh.
It validates all target meshes before changing the pose, so a missing child or
different morph-target count cannot leave a partial update.

The deterministic skin fixture now places the same skinned, morphed mesh twice.
Its package has 24 vertices, 36 indices, two placement records, five merged
material subsets, two skin joints, and one morph target. The native test verifies
that both Wicked meshes hold 12 vertices, 18 local indices, three local subsets,
and 12 skin-influence records; it also checks the shared armature and both
morph weights before and after pose completion.

Snapshot registration now follows the same placement contract. Its root owns
the shared armature, while each later placement has sliced skin and morph
streams and participates in animation-pose submission. The RenderScene smoke
forces a failed two-row transaction after creating an animated root and child,
checks rollback to the original object count, retries, submits a pose through
the C ABI, and verifies the child mesh receives the root armature and morph
weight before the snapshot is cleared.

## Validation

- `/opt/homebrew/bin/python3 scripts/gltf_skin_self_test.py` passed.
- `DEVELOPER_DIR=/Library/Developer/CommandLineTools CXX=/opt/homebrew/opt/llvm/bin/clang++ /opt/homebrew/bin/python3 scripts/test_geometry_subsets.py` passed: 87 loader cases, 0 failures, under AddressSanitizer and UndefinedBehaviorSanitizer.
- `DEVELOPER_DIR=/Library/Developer/CommandLineTools ELISA_COMPILER_BIN="/Users/torarinvikbjarko/Documents/Coding Projects/Elisa Projects/Elisa-compiler/scripts/elisac_stage1.sh" CXX=/opt/homebrew/opt/llvm/bin/clang++ /opt/homebrew/bin/python3 scripts/render_scene_native_smoke.py` passed. The SDL3/Metal smoke exercised the imported placement and animation tests; the packaged maze also ran outside the checkout.
- `DEVELOPER_DIR=/Library/Developer/CommandLineTools PYTHON_BIN=/opt/homebrew/bin/python3 ELISA_COMPILER_BIN="/Users/torarinvikbjarko/Documents/Coding Projects/Elisa Projects/Elisa-compiler/scripts/elisac_stage1.sh" CXX=/opt/homebrew/opt/llvm/bin/clang++ elisascript scripts/wicked_probe.elisascript build` passed, including the application lifecycle, SDL3/Metal render, and packaged-maze gates.
- On 2026-09-23, the SDL3/Metal RenderScene native smoke passed with animated snapshot placement, rollback, pose submission and cleanup coverage. Its separate maze application and packaged-maze stages were skipped because the preceding run had stalled in Metal teardown after the native gate.
- `python3 scripts/check_source_length.py`, `python3 scripts/check_module_hygiene.py`, and `git diff --check` passed.

The importer still accepts one glTF skin. It ignores skinned mesh-node
transforms, as required by glTF, while retaining the authored transforms in
placement metadata. Transformed non-joint ancestors of joint nodes are rejected
because the runtime rig does not preserve those nodes. Multiple rigs in one
scene remain unsupported. Static snapshot placement uploads are covered in
[`snapshot-mesh-placements.md`](snapshot-mesh-placements.md). This change does
not claim performance measurements. The two-placement animation fixture now
gives its second skinned mesh node a nonidentity transform, and the native
smoke confirms its uploaded positions match the first placement.
