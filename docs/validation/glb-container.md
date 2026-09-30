# GLB container (M06, first slice)

`GlbContainer` (`src/assets/glb_container.elisa`) reads the binary glTF
container in memory: the 12-byte header, the JSON chunk and an optional BIN
chunk. `parse` returns where each payload lies without copying anything.
`patch_bin` rewrites a byte range of the BIN payload in place, and
`read_bin` copies one out. This is the base of M06's round-trip rule: an
exported file is the loaded image with only the edited ranges rewritten, so
untouched nodes, meshes, skins and buffers are byte-identical.

Malformed images fail with a typed `GlbError`:

- `TooShort`: fewer than 20 bytes, or more than 2^32 − 1;
- `BadMagic` / `BadVersion`: not `glTF`, or not version 2;
- `LengthMismatch`: the header length is not the image size;
- `BadJsonChunk`: the JSON chunk is empty, overruns the image or has the
  wrong type;
- `BadBinChunk`: the chunk after the JSON overruns the image or is not BIN;
- `Misaligned`: a chunk length is not a multiple of four;
- `TrailingBytes`: bytes follow the BIN chunk;
- `RangeOutside`: a patch or read range is not inside the BIN payload.

A rejected patch leaves the image unchanged.

## Evidence

- `test/assets_glb_container.elisa` (in the gate) builds a GLB in memory
  and checks the layout of a file with and without BIN. It checks that an
  unedited copy is byte-identical and that a 4-byte patch changes exactly 4
  bytes. It checks that ranges past either payload end, and patches on a
  file with no BIN chunk, are rejected and leave the image unchanged. It
  also checks one malformed image for every parse error.
- `proof/glb_layout_index.elisa`, 73/73 with all obligations replayed.
  `GlbLayoutIndex::chunk_end` is proved never to report a chunk end past
  the image or before its 8-byte header, and to equal the header offset
  plus 8 plus the length. `range_inside` is proved to accept only ranges
  that start inside the payload, have a non-negative length and end inside
  it. The converse (every fitting chunk or range is accepted) did not prove.
  Its summaries timed out in the prover's fact budget, so the test covers
  it instead.

## Open

The JSON chunk is not parsed yet. Mapping animation channels to their
accessor byte ranges, loading FBX into an editable skeleton, and checking
against real exported files are the next M06 slices.
