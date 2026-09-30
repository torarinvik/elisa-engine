# glTF decimal numbers (M06, sixth slice)

`GltfNumber` (`src/assets/gltf_number.elisa`) reads a JSON number from a byte
range. It is the reader for decimal node transforms such as rest TRS.

- `valid(bytes, begin, end)` accepts exactly the RFC 8259 number grammar.
  It rejects leading zeros, a bare `-`, a trailing `.` or `e`, a leading `+`
  and ranges that fall outside the buffer.
- `value` keeps up to 18 significant digits exactly in an i64. It skips
  leading zeros, counts integer digits that were dropped and ignores
  dropped fraction digits. It then scales by an exact power of ten.
  - With at most 15 digits and |power| <= 22, the result is correctly
    rounded. This is the common case for glTF.
  - Otherwise the result is within a few ulps of the reference.
  - Negative zero keeps its sign.

## Proof

`proof/gltf_number_index.elisa` proves `GltfNumberIndex` (36/36, all replayed):

- the 18-digit keep bound;
- the saturating exponent accumulator, which stays in [0, 9999];
- the decimal-power formula.

The float scaling is tested only.

## Tests

`test/assets_gltf_number.elisa` checks 20 values against Python `float()`,
within one ulp. The values include a subnormal, a 21-digit integer and a
22-digit fraction. It also checks 13 invalid inputs, a sub-range of a larger
buffer and the sign of `-0`.

Each of these mutants breaks the test: the digit transitions, the decimal point, the
sign, the power-of-two squaring, the subnormal split, and the dropped-digit and
leading-zero accounting.
