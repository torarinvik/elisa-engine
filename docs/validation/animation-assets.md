# Animation asset contract validation

`src/animation/assets.elisa` is the current Elisa-owned validation boundary for
normalized skeletons and clips. Skeletons have stable positive joint IDs,
parent-before-child ordering, metre-space rest transforms, and inverse-bind
matrices that must invert the evaluated rest pose. Clips carry stable IDs,
rig identity, duration and tick rate, ordered events, and a track for every
joint. Track transforms and all timing values are checked before sampling.

The fixed joint capacity is shared by `AnimationLimits`, the Elisa pose and
sampler, the FBX skin cooker, and the Wicked pose bridge. Asset reads pass
large skeleton and clip records by reference. `test/anim_assets.elisa` is
included by the existing animation-state test in `scripts/check.elisascript`;
it covers invalid parent order, duplicate joint IDs, bad inverse binds and
units, missing tracks, unordered events, incompatible rigs, sampled poses,
and out-of-range sampling.

This is an in-memory contract prototype, not a production cooked animation
format. `Sampler::ClipData` stores keys in one contiguous dynamic buffer, with
ordered contiguous tracks and binary-search sampling. Loading can append up to
262,144 keys per clip; sampling does not allocate. Callers append tracks in
ascending joint order and keys in non-decreasing tick order. A serialized
animation package, FBX animation import, and a production ozz runtime path
remain open under C01/C02/A09.

Validation command:

```sh
DEVELOPER_DIR="$(xcode-select -p)" ../Elisa-compiler/scripts/elisac_stage1.sh \
  -emit exe -o build/anim-state-test test/anim_state.elisa
build/anim-state-test
```
