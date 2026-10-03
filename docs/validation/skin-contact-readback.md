# Current-skin contact readback (in development)

`read_skinned_vertices` requests up to 64 mesh-local vertex IDs and returns
Elisa model/world points under the owner-thread scene lock. The native path uses
current joint worlds, inverse binds, armature world removal and mesh placement
world transform, matching Wicked linear blend skinning. It avoids render-frame
boneData lag after pose override. It performs no allocation, playback advance
or root-motion consumption. Failed native reads leave caller output untouched.

Vertex IDs and mesh placement are supplied by the caller. This is not a cached
sole-region selector or contact-state solver. Active morphs currently reject
rather than silently report an incorrect unmorphed surface. Four/eight influence
arrays are handled; finite points, cluster indices, weights and invertibility
are validated. Native C++ syntax checks pass both with and without test probes; module hygiene passes all 129 production modules. Fresh linked build passes with compiler `b841e64`; native integration tests pass on both cooked boxers.

The integration test also checks native failed-read atomicity after a valid first vertex followed by an invalid second vertex, invalid placement, zero requests and excessive capacity. These tests pass on both cooked boxers.

Evidence: game `build/skin-readback-native-test/validation.json`, `build/skin-readback-{black,white}-native-run.log`, and `build/skin-readback-native-build.log`. All 17 engine build CLI tests pass after enabling renderer probes with `--native-test-probes`. Validation covers four vertices, not whole-foot clearance or GPU-skin agreement. Cached regions, morph deformation, contact state, sliding resistance and gameplay integration remain required.
