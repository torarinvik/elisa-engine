# GLB document load and round-trip (M06)

Date: 2026-09-30. Branch: `mocap-track`.

## What is covered

- **`GlbLayout`** (`src/assets/glb_layout.elisa`) is the integer bounds policy for chunks and accessors. `proof/glb_layout.elisa` proves two things: every accepted range lies inside its container, and an accepted accessor's bytes lie inside both its view and the BIN chunk. The prover discharges 76 of 76 obligations.
  - Negative control: removing three of the guards makes 5 obligations fail, as expected.
- **`GlbJson`** (`src/assets/glb_json.elisa`) is a flat, strict RFC 8259 tokenizer. It builds no DOM, so the file bytes stay the only copy.
- **`GlbDocument`** (`src/assets/glb_document.elisa`) does the loading, editing and saving.
  - It checks the header, the JSON chunk and the BIN chunk.
  - Nodes: name, parent and rest TRS. The hierarchy is validated, so self-parenting, a second parent and cycles are rejected.
  - Animations: each channel's sampler is decoded to f32 tracks. Interpolation can be LINEAR, STEP or CUBICSPLINE, and element counts are checked per path. Key times must not decrease.
  - `set_track_value` writes the f32 little-endian bytes in place, so the file length never changes.

## Test

`test/assets_glb_document.elisa` (registered in `scripts/check.elisascript`):

```
ELISA_ALLOW_STALE_STAGE1=1 elisac-stage1 -emit exe -o build/assets_glb_document-test test/assets_glb_document.elisa
./build/assets_glb_document-test   # exit 0 = pass
/private/tmp/claude-501/mc/elisa-proof "$PWD/proof/glb_layout.elisa"
```

- **Synthetic GLB (built in the test):**
  - Names, parents and rest values are read correctly, and a shared key-time accessor is decoded only once.
  - An unedited document equals its source byte for byte.
  - One edit changes only its 4 bytes inside the output accessor, and the edited bytes re-parse to the new value.
  - Edits that are out of range or NaN are refused.
- **Adversarial cases (each checks its typed variant):**
  - Container: bad magic, version 1, length mismatch, a JSON chunk past the end, a short BIN chunk, an 8-byte file, and a wrong chunk type.
  - Accessors: count past the view, a view past BIN, a view offset far out, u16 components, a sparse accessor, a byteStride, and a non-zero buffer.
  - JSON and hierarchy: a trailing comma, a child out of range, and a cycle.
  - Channels: a missing sampler, a rotation mapped to VEC3, and node or accessor indices out of range.
  - Every truncated prefix of a valid file is rejected.
  - Loading a missing path reports `OpenFailed`.
- **Real file** (`../elisa-boxing-game/build/dual-stance/black/black-boxer.glb`, read only; skipped if absent):
  - Loads 54 nodes and 28 animations.
  - An unedited save is byte-identical (`cmp` against the source also matches).
  - The test negates `w` on every key of the `jab` RightForeArm rotation. The diff stays inside that accessor, the length is unchanged, and the saved file reloads to the edited values.

## Limits

- FBX, `.gltf` with external buffers, and sparse, strided, normalized or non-float animation accessors are rejected. Accessors that are not animation inputs or outputs are not decoded, so they are always preserved.
- Key counts cannot change, because edits are in place. Adding or removing keys would need a BIN re-layout plus JSON rewrite, which is not done.
- JSON string escapes are not decoded, so names are compared as they are spelled in the file.
- A load reads the whole file into memory: about 0.2 s for 55 MB.
