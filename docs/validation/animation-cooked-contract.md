# Cooked animation contract (elisa-anim-v1)

C01 moves the skeleton and clip contract from in-memory structs to cooked
bytes. The glTF cooker writes `elisa-anim-v1`, and `src/animation/package.elisa`
validates it before anything is sampled.

## Format

All fields are little-endian.

- **Header (32 bytes).** Magic `EANM`, version 1, total bytes, an FNV-1a32
  checksum of bytes 16 onward, rig id, joint count (1–64), clip count (0–24),
  and metres per unit (must be 1.0).
- **Joint record (112 bytes each).** Joint id (FNV of the name), parent index
  (-1 for a root, otherwise an earlier joint), rest translation/rotation/scale,
  and a column-major inverse-bind matrix. The cooker derives the inverse bind
  from the rest pose and refuses authored matrices that disagree by more than
  1e-3.
- **Clip records.** A 24-byte header (clip id, rig id, duration ticks, ticks
  per second, track count, event count), then one track per joint in joint
  order. Each track is a joint index, a key count (at least 1), and 44-byte keys
  (tick plus transform). Then 8-byte events.
- **Rig id.** A stable hash of the joint records. A clip names the rig it was
  cooked for.

The bounds are 64 MiB per image, 262,144 keys per clip and 32 events per clip.

## Load order

`AnimationPackage::layout` checks, in this order:

1. magic, version, length and checksum;
2. counts;
3. that the joint region fits;
4. that the stored rig id matches the joint records;
5. that every clip's tracks and events fit, with no trailing bytes.

`decode_skeleton` rejects bad parents (`InvalidParent`) and duplicate joint ids
(`DuplicateJoint`) before running `skeleton_asset_valid`.

`decode_clip` takes the decoded skeleton and checks:

- that the rig matches (`IncompatibleClip`);
- that the track count is right (`MissingTrack` / `IncompatibleClip`);
- that tracks are in joint order;
- that keys and events are valid (`InvalidClip`).

It runs `clip_asset_valid` last. Only a clip returned from `decode_clip`
reaches the sampler.

## Evidence

- `scripts/test_animation_contract.py` checks that the cooker refuses
  malformed rigs and clips, and that `examples/character_course/rigs/guide_rig.anim`
  matches a fresh cook.
- `test/animation_cooked_contract.elisa` loads the committed cooked rig and
  samples it. It then edits the bytes to cover invalid parents, duplicate
  joints, stale rig ids, missing and reordered tracks, clips cooked for another
  rig, bad keys, truncation, trailing bytes, tampering, and wrong
  magic/version/length/counts/units. Each edit must fail with its expected
  error.
- `proof/animation_package_index.elisa` proves that the integer layout rules in
  `src/animation/package_index.elisa` are sound. Accepted spans stay inside the
  image, parents precede children, tracks match joints in order, and key and
  event counts stay bounded.
- A mutation run that disabled the duplicate, checksum, rig-mismatch,
  trailing-bytes and rig-id checks failed the test each time.

## Gaps

Production ozz sampling is C02. FBX cooking into this format is A09.
