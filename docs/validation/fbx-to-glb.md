# FBX takes as editable GLB (M06)

`native/fbx_to_glb.c` (`elisa_fbx_to_glb(input, output, rate)`) loads an FBX with the pinned ufbx and turns it into an editable GLB.
- It uses the cook path's strictness, depth and memory limits. The axis and unit change (right-handed, Y up, metres) is baked into the nodes.
- Every animation stack is baked to linear per-node translation, rotation and scale tracks at `rate` samples per second (default 30).
- The GLB holds the node hierarchy with rest TRS and one animation per stack, using float accessors only. `GlbDocument`, `MocapPipeline` and `MocapThumbnail` therefore treat it like any other take.
- Meshes and skins are not carried over: the output is the editable animation, not a render asset.
- Nothing is written unless the whole take converts. Failure codes: load −1, bake −2, memory −3, write −4, non-finite value −5.

`build/mocap-clean` (see mocap-pipeline.md) accepts `*.fbx` beside `*.glb`. It writes `NAME.glb` and `NAME.png`, and `MOCAP_CONVERT_ONLY=1` skips the cleaning of FBX takes.

## Evidence

`python3 scripts/fbx_glb_smoke.py` uses `bladed_cross.fbx`, a 1.2 MB Mixamo-rig take from ../elisa-boxing-game (read only). It skips when that file or Blender is absent.
- **Reference.** Blender 5.2's own FBX importer (tools/fbx_glb_reference.py) gives each bone head's world position at four frames. The smoke samples our GLB in Python and composes world positions up the parents. Over 208 joint samples, the worst difference is **0.0013 mm** (tolerance 1 mm). The GLB has 54 nodes and 159 channels.
- **Determinism.** Two conversions are byte-identical.
- **Malformed input.** A 40 kB truncated FBX fails with `FAIL truncated.fbx -101` and exit code 1, and leaves the output folder empty.
- **Clean and thumbnail.** Cleaning plus a thumbnail of the converted take succeeds.
- **Mutant.** Converting with a 0.01 unit scale fails the comparison with exit 16 (185 m error).

## Limits

- Resampling at 30 Hz (or `MOCAP_FBX_RATE`) replaces the FBX curves with linear keys.
- Blend shapes and custom properties are not baked.
